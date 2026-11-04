#include "doa_math.h"
#include <math.h>
#include <string.h>

static const unsigned pair_i[DOA_PAIRS] = {0,0,0,0,0,0,0,1,1,1,1,1,1,2,2,2,2,2,3,3,3,3,4,4,4,5,5,6};
static const unsigned pair_j[DOA_PAIRS] = {1,2,3,4,5,6,7,2,3,4,5,6,7,3,4,5,6,7,4,5,6,7,5,6,7,6,7,7};
static double delay_table[DOA_SCAN_POINTS][DOA_PAIRS];
static double averaged[DOA_SCAN_POINTS];
static int history_valid;
static double current[DOA_SCAN_POINTS], smooth[DOA_SCAN_POINTS];
typedef struct {
    unsigned bin;
    double level, prominence;
} Peak;
typedef struct {
    double angle;
    uint32_t id;
    unsigned hits, misses, recent, born, observed, strong_recent;
    double level, prominence;
    int used, confirmed;
} Track;
static Track tracks[DOA_MAX_SOURCES];
static uint32_t next_source_id;
static unsigned instantaneous_count;
static unsigned evidence_mask, strong_evidence_mask;
static unsigned quiet_frames;
static DoaDebug debug;
static unsigned relocation_frames, relocation_angle;

double doa_expected_lag(unsigned pair, double angle_deg)
{
    const double pi = 3.14159265358979323846;
    double a, ai, aj, dx, dy;
    if (pair >= DOA_PAIRS) return 0.0;
    a = angle_deg * pi / 180.0;
    ai = (90.0 + 45.0 * pair_i[pair]) * pi / 180.0;
    aj = (90.0 + 45.0 * pair_j[pair]) * pi / 180.0;
    dx = DOA_RADIUS_MM * (cos(aj) - cos(ai));
    dy = DOA_RADIUS_MM * (sin(aj) - sin(ai));
    /* R_ij(l)=sum x_i[t] x_j[t-l]. Positive delay is earlier at j. */
    return (dx * cos(a) + dy * sin(a)) *
           DOA_SAMPLE_RATE / DOA_SOUND_SPEED_MM_S;
}

void doa_math_reset(void)
{
    history_valid = 0; quiet_frames=0;
    relocation_frames = 0;
    memset(averaged, 0, sizeof(averaged));
    memset(tracks, 0, sizeof(tracks));
}

void doa_math_init(void)
{
    unsigned a, p;
    for (a = 0; a < DOA_SCAN_POINTS; ++a)
        for (p = 0; p < DOA_PAIRS; ++p)
            delay_table[a][p] = doa_expected_lag(p, a + DOA_SECTOR_FIRST);
    doa_math_reset();
    next_source_id = 0U;
    memset(&debug,0,sizeof(debug));
}

static double clamp(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static double angle_difference(double a, double b)
{
    double d = a-b;
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

/* Local prominence is measured against BOTH neighbouring valleys. Comparing
 * the two highest directions globally would reject two equally loud sources.
 * The bounded window and peak spacing deliberately leave unresolved lobes as
 * one source. No source is fabricated just to fill the four output slots. */
static unsigned find_peaks(Peak peaks[DOA_MAX_SOURCES], double best)
{
    unsigned count=0, a, k, rank;
    for (rank=0; rank<DOA_MAX_SOURCES; ++rank) {
        Peak chosen={0,0.0,0.0};
        int found=0;
        for (a=0; a<DOA_SCAN_POINTS; ++a) {
            double level=smooth[a], left=level, right=level, prominence;
            int separated=1;
            if (level < DOA_MIN_PEAK_COHERENCE || level < 0.30*best ||
                level <= smooth[(a+359U)%360U] || level < smooth[(a+1U)%360U])
                continue;
            for (k=0; k<count; ++k)
                if (fabs(angle_difference(a,peaks[k].bin)) < DOA_PEAK_SEPARATION_DEG)
                    separated=0;
            if (!separated) continue;
            for (k=1; k<=DOA_PROMINENCE_WINDOW_DEG; ++k) {
                double v=smooth[(a+360U-k)%360U];
                if (v > level) break;
                if (v < left) left=v;
            }
            for (k=1; k<=DOA_PROMINENCE_WINDOW_DEG; ++k) {
                double v=smooth[(a+k)%360U];
                if (v > level) break;
                if (v < right) right=v;
            }
            prominence=level-(left>right?left:right);
            if (prominence < DOA_MIN_PROMINENCE || prominence < 0.04*level) continue;
            if (!found || level>chosen.level) {
                chosen.bin=a; chosen.level=level; chosen.prominence=prominence;
                found=1;
            }
        }
        if (!found) break;
        peaks[count++]=chosen;
    }
    return count;
}

/* Music/speech peaks are intermittent. Confirm secondary tracks from three
 * observations within eight processed frames; expire after 24 misses (~512ms
 * at nominal frame rate). Silence/fault/incoherence still clears immediately.
 * No direction is synthesized: every track starts at an observed local peak. */
static void track_peaks(const Peak *peaks, unsigned count, DoaResult *result)
{
    int match[DOA_MAX_SOURCES]={-1,-1,-1,-1};
    unsigned used[DOA_MAX_SOURCES]={0,0,0,0}, p,t,rank;
    for(t=0;t<DOA_MAX_SOURCES;++t)
        if(tracks[t].used) { tracks[t].recent=(tracks[t].recent<<1)&65535U; tracks[t].strong_recent=(tracks[t].strong_recent<<1)&255U; }
    for(rank=0;rank<count;++rank) {
        double nearest=25.0;
        int bp=-1,bt=-1;
        for(p=0;p<count;++p) if(match[p]<0)
            for(t=0;t<DOA_MAX_SOURCES;++t) if(tracks[t].used && !used[t]) {
                double dist=fabs(angle_difference(peaks[p].bin,tracks[t].angle));
                if(dist<nearest){nearest=dist;bp=(int)p;bt=(int)t;}
            }
        if(bp<0) break;
        match[bp]=bt;used[bt]=1;
    }
    for(t=0;t<DOA_MAX_SOURCES;++t) if(tracks[t].used && !used[t]) {
        tracks[t].level*=0.97;
        if(++tracks[t].misses>=24U) {tracks[t].used=0;++debug.expired;}
    }
    for(p=0;p<count;++p) {
        int fresh=0;
        if(match[p]<0) {
            int slot=-1;
            for(t=0;t<DOA_MAX_SOURCES;++t) if(!tracks[t].used){slot=(int)t;break;}
            if(slot<0) for(t=0;t<DOA_MAX_SOURCES;++t) if(!used[t]){slot=(int)t;break;}
            if(slot<0) continue;
            match[p]=slot;used[slot]=1;fresh=1;
            memset(&tracks[slot],0,sizeof(tracks[slot]));
            tracks[slot].id=next_source_id++;tracks[slot].used=1;
            tracks[slot].angle=peaks[p].bin; tracks[slot].born=debug.frames;
        }
        t=(unsigned)match[p];
        if(!fresh) tracks[t].angle+=0.45*angle_difference(peaks[p].bin,tracks[t].angle);
        if(tracks[t].angle<0.0) tracks[t].angle+=360.0;
        if(tracks[t].angle>=360.0) tracks[t].angle-=360.0;
        /* A lone jumping peak is not evidence of a second source while a
         * confirmed track is retained. Only instantaneous multi-peak frames contribute to
         * secondary confirmation. Existing confirmed tracks can bridge gaps. */
        int other_confirmed=0;
        for(unsigned q=0;q<DOA_MAX_SOURCES;++q)
            if(q!=t && tracks[q].used && tracks[q].confirmed) other_confirmed=1;
        tracks[t].misses=0;
        unsigned revisited=0;
        for(unsigned q=0;q<DOA_MAX_SOURCES;++q)
            if(q!=t && tracks[q].used && tracks[q].confirmed &&
               tracks[q].observed>tracks[t].born && debug.frames-tracks[q].observed<=8U) revisited=1U;
        if(evidence_mask & (1U<<p)) tracks[t].observed=debug.frames;
        if((evidence_mask & (1U<<p)) && (instantaneous_count>1U || revisited || !other_confirmed)) {
            tracks[t].recent|=1U;
            if(strong_evidence_mask & (1U<<p)) tracks[t].strong_recent|=1U;
        }
        tracks[t].level=peaks[p].level;tracks[t].prominence=peaks[p].prominence;
        tracks[t].hits=0;
        for(unsigned bits=tracks[t].recent;bits;bits>>=1) tracks[t].hits+=bits&1U;
        unsigned strong_hits=0;
        for(unsigned bits=tracks[t].strong_recent;bits;bits>>=1) strong_hits+=bits&1U;
        if((count==1U && !other_confirmed && (evidence_mask & (1U<<p))) || strong_hits>=3U || tracks[t].hits>=12U) tracks[t].confirmed=1;
    }
    /* Do not publish two identities inside the estimator's resolvable
     * separation. Preserve the older ID when a drifting peak rejoins it. */
    for(t=0;t<DOA_MAX_SOURCES;++t) if(tracks[t].used)
        for(unsigned q=t+1U;q<DOA_MAX_SOURCES;++q) if(tracks[q].used &&
            fabs(angle_difference(tracks[t].angle,tracks[q].angle))<DOA_PEAK_SEPARATION_DEG) {
            unsigned keep=tracks[t].confirmed!=tracks[q].confirmed ?
                (tracks[t].confirmed?t:q) : (tracks[t].id<tracks[q].id?t:q);
            unsigned drop=keep==t?q:t;
            if(tracks[drop].observed>tracks[keep].observed) {
                tracks[keep].angle=tracks[drop].angle;
                tracks[keep].observed=tracks[drop].observed;
                tracks[keep].level=tracks[drop].level;
                tracks[keep].prominence=tracks[drop].prominence;
            }
            tracks[keep].confirmed|=tracks[drop].confirmed;
            tracks[keep].recent|=tracks[drop].recent;
            if(tracks[drop].misses<tracks[keep].misses) tracks[keep].misses=tracks[drop].misses;
            tracks[drop].used=0;
            if(drop==t) break;
        }
    /* Rank current and briefly retained confirmed tracks by measured strength. */
    memset(used,0,sizeof(used));
    for(rank=0;rank<DOA_MAX_SOURCES;++rank) {
        int best=-1;
        for(t=0;t<DOA_MAX_SOURCES;++t)
            if(tracks[t].used && tracks[t].confirmed && !used[t] &&
               (best<0 || tracks[t].level>tracks[best].level)) best=(int)t;
        if(best<0) break;
        t=(unsigned)best;used[t]=1;
        DoaSource *source=&result->sources[result->source_count++];
        source->source_id=tracks[t].id;
        source->angle_deg=(uint16_t)((unsigned)(tracks[t].angle+0.5)%360U);
        source->strength=(uint16_t)clamp(tracks[t].level*1000.0,0.0,1000.0);
        if(result->source_count==1U)
            result->prominence_milli=(uint16_t)clamp(tracks[t].prominence*1000.0,0.0,1000.0);
    }
    if(result->source_count) {
        result->valid=1U;result->angle_deg=result->sources[0].angle_deg;
        result->strength=result->sources[0].strength;
    }
}

void doa_math_process(const DoaFrame *frame, DoaResult *result)
{
    double normalizer[DOA_PAIRS];
    double best = -2.0, max_rms = 0.0, min_rms = 1e20;
    unsigned a, c, p;
    Peak peaks[DOA_MAX_SOURCES];
    memset(result, 0, sizeof(*result));
    ++debug.frames;
    debug.reason=0U;debug.stable_count=debug.instant_count=debug.candidate_count=0U;
    debug.evidence_mask=0U;
    memset(debug.stable,0,sizeof(debug.stable));
    memset(debug.instant,0,sizeof(debug.instant));
    memset(debug.candidates,0,sizeof(debug.candidates));
    memset(debug.tracks,0,sizeof(debug.tracks));
    for (c = 0; c < DOA_CHANNELS; ++c) {
        double rms = sqrt((double)frame->energy[c] / DOA_FRAME_SAMPLES);
        result->rms[c] = (uint16_t)clamp(rms + 0.5, 0.0, 65535.0);
        if (rms >= DOA_MIN_RMS) result->active_mask |= (uint8_t)(1U << c);
        if (rms > max_rms) max_rms = rms;
        if (rms < min_rms) min_rms = rms;
    }
    /* Eight working microphones are required; never localize a floating/stuck
     * channel or keep a stale marker while one channel is silent. */
    if (result->active_mask != 255U || max_rms > 32.0 * min_rms) {
        if(min_rms==0.0 || max_rms>32.0*min_rms || ++quiet_frames>=12U) doa_math_reset();
        debug.reason=result->active_mask!=255U?1U:2U;
        ++debug.resets[debug.reason];
        return;
    }
    quiet_frames=0U;
    for (p = 0; p < DOA_PAIRS; ++p)
        normalizer[p] = sqrt((double)frame->energy[pair_i[p]] *
                             (double)frame->energy[pair_j[p]]);
    for (a = 0; a < DOA_SCAN_POINTS; ++a) {
        double score = 0.0;
        for (p = 0; p < DOA_PAIRS; ++p) {
            double x = delay_table[a][p] + DOA_MAX_LAG;
            unsigned index = (unsigned)floor(x);
            double fraction = x - index;
            /* Geometry is bounded inside +-18 samples for this board. */
            if (index >= DOA_LAGS-1U) index = DOA_LAGS-2U;
            score += clamp(((1.0-fraction) * frame->correlation[p][index] +
                             fraction * frame->correlation[p][index+1U]) /
                            normalizer[p], -1.0, 1.0);
        }
        current[a] = score / DOA_PAIRS;
    }
    /* Reject incoherent frames before temporal smoothing: old energy must not
     * turn disconnected microphones or independent noise into a source. */
    for (a = 0; a < DOA_SCAN_POINTS; ++a)
        if (current[a] > best) best = current[a];
    if (best < DOA_MIN_COHERENCE) {
        debug.reason=3U;++debug.resets[3];
        doa_math_reset();
        return;
    }
    for (a = 0; a < DOA_SCAN_POINTS; ++a) {
        averaged[a] = history_valid ? 0.96 * averaged[a] + 0.04 * current[a]
                                    : current[a];

    }
    for(a=0;a<360U;++a) smooth[a]=(averaged[(a+359U)%360U]+2.0*averaged[a]+averaged[(a+1U)%360U])*0.25;
    history_valid = 1;
    result->coherence_milli = (uint16_t)clamp(best * 1000.0, 0.0, 1000.0);
    /* This is normalized steered correlation, a relative directional score,
     * not a calibrated SPL/physical energy measurement. */
    for (a = DOA_SECTOR_FIRST; a <= DOA_SECTOR_LAST; a += 5U)
        result->spectrum[a/5U] = (uint16_t)clamp(
            averaged[a-DOA_SECTOR_FIRST] * 1000.0, 0.0, 1000.0);
    /* Slow (~0.5s EMA) spatial evidence admits a new direction. Once a
     * direction is confirmed, fresh local peaks update it without waiting for
     * the slow spectrum to separate again. This bridges music modulation while
     * preventing one transient peak from creating another persistent source.
     * A strong new instantaneous primary can enter probation before the
     * slow spectrum separates. Confirmation still needs repeated evidence.
     * No fixed angles or source count are encoded in this estimator. */
    unsigned stable_count=find_peaks(peaks,best);
    debug.stable_count=stable_count;
    for(unsigned i=0;i<stable_count;++i) {
        debug.stable[i].angle=peaks[i].bin;
        debug.stable[i].level=(int32_t)(peaks[i].level*1000.0);
        debug.stable[i].prominence=(int32_t)(peaks[i].prominence*1000.0);
    }
    Peak instant[DOA_MAX_SOURCES];
    for(a=0;a<360U;++a) smooth[a]=(current[(a+359U)%360U]+2.0*current[a]+current[(a+1U)%360U])*0.25;
    instantaneous_count=find_peaks(instant,best);
    debug.instant_count=instantaneous_count;
    for(unsigned i=0;i<instantaneous_count;++i) {
        debug.instant[i].angle=instant[i].bin;
        debug.instant[i].level=(int32_t)(instant[i].level*1000.0);
        debug.instant[i].prominence=(int32_t)(instant[i].prominence*1000.0);
    }
    /* A sustained lone direction jump is a relocation, not another source.
     * Six observations after the old direction has lacked fresh evidence
     * for six frames prevent slow spatial history
     * from trapping a single moving source at its old angle. Never use this
     * shortcut while multiple tracks are confirmed. */
    unsigned confirmed=0, sole=0;
    for(unsigned t=0;t<DOA_MAX_SOURCES;++t)
        if(tracks[t].used && tracks[t].confirmed){++confirmed;sole=t;}
    /* Two distinct slow peaks contradict a single moving-source reset. */
    if(stable_count<=1U && instantaneous_count==1U && confirmed==1U &&
       fabs(angle_difference(instant[0].bin,tracks[sole].angle))>=35.0) {
        if(relocation_frames && fabs(angle_difference(instant[0].bin,relocation_angle))<20.0)
            ++relocation_frames;
        else relocation_frames=1U;
        relocation_angle=instant[0].bin;
        if(relocation_frames>=6U && debug.frames-tracks[sole].observed>=6U) {
            debug.reason=4U;++debug.resets[4];
            memcpy(averaged,current,sizeof(averaged));
            memset(tracks,0,sizeof(tracks));
            peaks[0]=instant[0];stable_count=1U;relocation_frames=0U;
        }
    } else relocation_frames=0U;
    for(unsigned i=0;i<instantaneous_count;++i) {
        int tracked=0,slot=-1,separated=1;
        for(unsigned t=0;t<DOA_MAX_SOURCES;++t)
            if(tracks[t].used && tracks[t].confirmed && fabs(angle_difference(instant[i].bin,tracks[t].angle))<20.0)tracked=1;
        if(!tracked && !(i==0U && instant[i].level>=0.50))continue;
        for(unsigned j=0;j<stable_count;++j) {
            double d=fabs(angle_difference(instant[i].bin,peaks[j].bin));
            if(d<25.0)slot=(int)j;
            else if(d<DOA_PEAK_SEPARATION_DEG)separated=0;
        }
        if(slot>=0)peaks[slot]=instant[i];
        else if(separated && stable_count<DOA_MAX_SOURCES)peaks[stable_count++]=instant[i];
    }
    /* A multi-peak frame only confirms candidates actually matched to one
     * of its peaks. An unrelated secondary peak is not evidence here. */
    evidence_mask=0U;strong_evidence_mask=0U;
    for(unsigned i=0;i<instantaneous_count;++i) {
        double nearest=25.0; int slot=-1;
        for(unsigned j=0;j<stable_count;++j) if(!(evidence_mask&(1U<<j))) {
            double distance=fabs(angle_difference(instant[i].bin,peaks[j].bin));
            if(distance<nearest){nearest=distance;slot=(int)j;}
        }
        if(slot>=0) {
            evidence_mask|=1U<<(unsigned)slot;
            if(confirmed==0U || (instant[i].level>=0.30 && instant[i].level>=0.45*instant[0].level)) strong_evidence_mask|=1U<<(unsigned)slot;
        }
    }
    debug.evidence_mask=evidence_mask;debug.candidate_count=stable_count;
    for(unsigned i=0;i<stable_count;++i) {
        debug.candidates[i].angle=peaks[i].bin;
        debug.candidates[i].level=(int32_t)(peaks[i].level*1000.0);
        debug.candidates[i].prominence=(int32_t)(peaks[i].prominence*1000.0);
    }
    track_peaks(peaks,stable_count,result);
    for(unsigned t=0;t<DOA_MAX_SOURCES;++t) {
        debug.tracks[t].id=tracks[t].id;
        debug.tracks[t].angle=(uint32_t)(tracks[t].angle+0.5)%360U;
        debug.tracks[t].recent=tracks[t].recent;
        debug.tracks[t].misses=tracks[t].misses;
        debug.tracks[t].flags=(tracks[t].used?1U:0U)|(tracks[t].confirmed?2U:0U);
    }
}

void doa_math_debug(DoaDebug *out) { *out=debug; }

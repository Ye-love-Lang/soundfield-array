package com.example.soundfield.audio

/** Groups arbitrary TCP byte boundaries into complete, sample-aligned PCM blocks. */
class PcmChunker(private val blockBytes: Int = 960) {
    init { require(blockBytes > 0 && blockBytes % 2 == 0) }

    private var block = ByteArray(blockBytes)
    private var used = 0

    fun accept(bytes: ByteArray, length: Int, onBlock: (ByteArray) -> Unit) {
        require(length in 0..bytes.size)
        var offset = 0
        while (offset < length) {
            val amount = minOf(blockBytes - used, length - offset)
            bytes.copyInto(block, used, offset, offset + amount)
            used += amount
            offset += amount
            if (used == blockBytes) {
                onBlock(block)
                block = ByteArray(blockBytes)
                used = 0
            }
        }
    }
}

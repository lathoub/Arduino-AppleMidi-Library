// The recovery journal is the default resiliency tool for unreliable
// transport. In this section, we normatively define the roles that
// senders and receivers play in the recovery journal system.
//
// This section introduces the structure of the recovery journal and
// defines the bitfields of recovery journal headers. Appendices A and
// B complete the bitfield definition of the recovery journal.
//
// The recovery journal has a three-level structure:
//
// o Top-level header.
//
// o Channel and system journal headers. These headers encode recovery
//   information for a single voice channel (channel journal) or for
//   all system commands (system journal).
//
// o Chapters. Chapters describe recovery information for a single
//   MIDI command type.
//
int decodeJournalSection(RingBuffer<byte, Settings::MaxBufferSize> &buffer, size_t &i, size_t &minimumLen)
{
    conversionBuffer cb;

    minimumLen += 1;
    if (buffer.getLength() < minimumLen)
    {
        return PARSER_NOT_ENOUGH_DATA;
    }

    /* lets get the main flags from the recovery journal header */
    uint8_t flags = buffer.peek(i++);

    /* At the same place we find the total channels encoded in the channel journal */
    uint8_t totalChannels = (flags & RTP_MIDI_JS_MASK_TOTALCHANNELS) + 1;

    V_DEBUG_PRINT(F("totalChannels: "));
    V_DEBUG_PRINTLN(totalChannels);

    // sequenceNr
    minimumLen += 2;
    if (buffer.getLength() < minimumLen)
        return PARSER_NOT_ENOUGH_DATA;

    // The 16-bit Checkpoint Packet Seqnum header field codes the sequence
    // number of the checkpoint packet for this journal, in network byte
    // order (big-endian). The choice of the checkpoint packet sets the
    // depth of the checkpoint history for the journal (defined in Appendix A.1).
    //
    // Receivers may use the Checkpoint Packet Seqnum field of the packet
    // that ends a loss event to verify that the journal checkpoint history
    // covers the entire loss event. The checkpoint history covers the loss
    // event if the Checkpoint Packet Seqnum field is less than or equal to
    // one plus the highest RTP sequence number previously received on the
    // stream (modulo 2^16).
    cb.buffer[0] = buffer.peek(i++);
    cb.buffer[1] = buffer.peek(i++);
    uint16_t checkPoint = ntohs(cb.value16);

    // The S (single-packet loss) bit appears in most recovery journal
    // structures, including the recovery journal header. The S bit helps
    // receivers efficiently parse the recovery journal in the common case
    // of the loss of a single packet.
    if (flags & RTP_MIDI_JS_FLAG_S)
    {
    }

    // If the Y header bit is set to 1, the system journal appears in the
    // recovery journal, directly following the recovery journal header.
    if (flags & RTP_MIDI_JS_FLAG_Y)
    {
    }

    // If the A header bit is set to 1, the recovery journal ends with a
    // list of (TOTCHAN + 1) channel journals (the 4-bit TOTCHAN header
    // field is interpreted as an unsigned integer).
    if (flags & RTP_MIDI_JS_FLAG_A)
    {
        /* iterate through all the channels specified in header */

        minimumLen += 3;
        if (buffer.getLength() < minimumLen)
            return PARSER_NOT_ENOUGH_DATA;

        for (auto j = 0; j < totalChannels; j++)
        {
            cb.buffer[0] = buffer.peek(i++);
            cb.buffer[1] = buffer.peek(i++);
            cb.buffer[2] = buffer.peek(i++);
            cb.buffer[3] = 0x00;

            uint32_t chanflags = ntohl(cb.value32);
            uint16_t chanjourlen = (chanflags & RTP_MIDI_CJ_MASK_LENGTH) >> 8;

            /* Do we have a program change chapter? */
            if (chanflags & RTP_MIDI_CJ_FLAG_P)
            {
                minimumLen += 3;
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;
                i += 3;
            }

            /* Do we have a control chapter? */
            if (chanflags & RTP_MIDI_CJ_FLAG_C)
            {
            }

            /* Do we have a parameter changes? */
            if (chanflags & RTP_MIDI_CJ_FLAG_M)
            {
            }

            /* Do we have a pitch-wheel chapter? */
            if (chanflags & RTP_MIDI_CJ_FLAG_W)
            {
                minimumLen += 2;
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;
                i += 2;
            }

            /* Do we have a note on/off chapter? */
            if (chanflags & RTP_MIDI_CJ_FLAG_N)
            {
                minimumLen += 2;
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;

                cb.buffer[0] = buffer.peek(i++);
                cb.buffer[1] = buffer.peek(i++);
                const uint16_t header = ntohs(cb.value16);

                uint8_t logListCount = (header & RTP_MIDI_CJ_CHAPTER_N_MASK_LENGTH) >> 8;
                const uint8_t low = (header & RTP_MIDI_CJ_CHAPTER_N_MASK_LOW) >> 4;
                const uint8_t high = (header & RTP_MIDI_CJ_CHAPTER_N_MASK_HIGH);

                // how many offbits octets do we have?
                uint8_t offbitCount = 0;
                if (low <= high)
                    offbitCount = high - low + 1;
                else if ((low == 15) && (high == 0))
                    offbitCount = 0;
                else if ((low == 15) && (high == 1))
                    offbitCount = 0;
                else
                    return PARSER_UNEXPECTED_DATA; // (LOW > HIGH) value pairs MUST NOT appear in the header.

                // special case -> no offbit octets, but 128 note-logs
                if ((logListCount == 127) && (low == 15) && (high == 0))
                {
                    logListCount = 128;
                    // offbitCount should be 0 (empty)
                }

                minimumLen += ((logListCount * 2) + offbitCount);
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;

                i += ((logListCount * 2) + offbitCount);

                // // Log List
                // for (auto j = 0; j < logListCount; j++ ) {
                //     buffer.peek(i++);
                //     buffer.peek(i++);
                // }

                // // Offbit Octets
                // for (auto j = 0; j < offbitCount; j++ ) {
                //     buffer.peek(i++);
                // }
            }

            /* Do we have a note command extras chapter? */
            if (chanflags & RTP_MIDI_CJ_FLAG_E)
            {
                minimumLen += 1;
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;

                /* first we need to get the flags & length of this chapter */
                uint8_t header = buffer.peek(i++);
                uint8_t log_count = header & RTP_MIDI_CJ_CHAPTER_E_MASK_LENGTH;

                log_count++;

                minimumLen += (log_count * 2);
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;

                for (auto j = 0; j < log_count; j++ ) {
                    uint8_t note = buffer.peek(i++) & 0x7f;
                    uint8_t octet = buffer.peek(i++);
                    uint8_t count_vel = octet & 0x7f;
                }
            }

            /* Do we have channel aftertouch chapter? */
            if (chanflags & RTP_MIDI_CJ_FLAG_T)
            {
                minimumLen += 1;
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;

                i += 1;
            }

            /* Do we have a poly aftertouch chapter? */
            if (chanflags & RTP_MIDI_CJ_FLAG_A)
            {
                minimumLen += 2;
                if (buffer.getLength() < minimumLen)
                    return PARSER_NOT_ENOUGH_DATA;

                /* first we need to get the flags & length of this chapter */
                uint8_t flags = buffer.peek(i++);
                uint8_t log_count = flags & RTP_MIDI_CJ_CHAPTER_A_MASK_LENGTH;

                /* count is encoded n+1 */
                log_count++;

                for (auto j = 0; j < log_count; j++ ) {
                    uint8_t note = buffer.peek(i++);
                    uint8_t pressure = buffer.peek(i++);
                }
            }
        }
    }

    // The H bit indicates if MIDI channels in the stream have been
    // configured to use the enhanced Chapter C encoding
    //
    // By default, the payload format does not use enhanced Chapter C
    // encoding. In this default case, the H bit MUST be set to 0 for all
    // packets in the stream.
    if (flags & RTP_MIDI_JS_FLAG_H)
    {
    }
}
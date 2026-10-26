src=open('ref/rp2350-amiga/src/Host.c').read().split('\n')
body='\n'.join(src[453:1085])   # lines 454..1085 (the #endif at 1086 is left out)
assert body.startswith('// ── Core 1: consumer'), body[:30]
def strip_ifblock(text, cond):
    out=[];skip=0;depth=0
    for line in text.split('\n'):
        st=line.strip()
        if skip:
            if st.startswith('#if'): depth+=1
            elif st.startswith('#endif'):
                if depth==0: skip=0; continue
                depth-=1
            continue
        if st==cond: skip=1; depth=0; continue
        out.append(line)
    return '\n'.join(out)
body=strip_ifblock(body,'#if OMEGA_RTG')
def rep(a,b):
    global body
    assert a in body, a[:60]
    body=body.replace(a,b)
rep("    c1_field = (layout >> 25) & 1u ? (int)((layout >> 26) & 1u) : -1;",
 "    // frank-amiga: interlace weaving is not ported yet; fields are drawn\n    // as progressive frames.\n    c1_field = -1;")
rep("""    if (c1_field >= 0) {
        // Interlace: draw the field over its own buffer, no swap to wait
        // for (scanout weaves both buffers).
        c1_image = hostCaptureHold ? NULL
                 : dvi_display_field_acquire(c1_field, &c1_record,
                                             &c1_prev_record);
        c1_buffer = c1_field;
    } else {
        dvi_display_weave_stop();""","""    {""")
rep("""    if (c1_field >= 0)
        dvi_display_field_publish(border);
    else
        dvi_display_direct_publish(border);""","""    dvi_display_direct_publish(border);""")
rep("""            case MSG_ROW_END:
                c1RowEnd(row);
                length = 1;
                break;""","""            case MSG_ROW_END:
                c1RowEnd(row);
                length = 1;
                break;
            case MSG_AUDIO: {
                // frank-amiga: Paula's line record (omega/Audio.h), mixed
                // here (omega/AudioMix.c) into the I2S output.
                const uint32_t n = header >> 23;
                denise_audio_lines++;
                if (n <= AUDIO_LINE_MAX_WORDS) {
                    uint32_t words[AUDIO_LINE_MAX_WORDS];
                    uint32_t s = ringSlot(tail);
                    for (uint32_t i = 0; i < n; ++i) {
                        if (++s == RING_WORDS) s = 0;
                        words[i] = ring[s];
                    }
                    if (words[0] & AUDIO_LINE_LONG) denise_audio_long++;
                    audioMixLine(words, (int)n);
                }
                length = 1 + n;
                break;
            }""")
rep("""            default:
                panic("host: bad core-1 ring message %08lx",
                      (unsigned long)header);
            }""","""            default:
                // frank-amiga: a corrupt stream (lost link data). Drop
                // everything queued; stream.c resynchronises at a frame.
                denise_stream_corrupt(header);
                head = __atomic_load_n(&ring_head, __ATOMIC_ACQUIRE);
                tail = head;
                __atomic_store_n(&ring_tail, tail, __ATOMIC_RELEASE);
                c1_image = NULL;
                continue;
            }""")
head=open('master/src/denise/denise_c1_head.inc').read()
open('master/src/denise/denise_c1.c','w').write(head+body+'\n')
print(len(body.split('\n')),'lines')
# frank-amiga entry point for the planar tables (static upstream).
with open('master/src/denise/denise_c1.c','a') as f:
    f.write('\nvoid denise_c1_init(void) {\n    hostInitPlanarTables();\n}\n')

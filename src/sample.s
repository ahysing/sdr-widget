	.file	"sample.c"
	.section	.debug_abbrev,"",@progbits
.Ldebug_abbrev0:
	.section	.debug_info,"",@progbits
.Ldebug_info0:
	.section	.debug_line,"",@progbits
.Ldebug_line0:
	.text
.Ltext0:
	.section	.text.uballe,"ax",@progbits
	.align	1
	.global	uballe
	.type	uballe, @function
uballe:
.LFB0:
	.file 1 "sample.c"
	.loc 1 4 0
	# args = 0, frame = 0, pretend = 0
	# frame_needed = 0, leaf_function = 1
	# uses_anonymous_args = 0
.LVL0:
	stm	--sp, r7, lr
.LCFI0:
	.loc 1 5 0
	mov	lr, r12
	asr     r12, 16
.LVL1:
	andl	lr, lo(-256)
	bfextu	r7, r12, 0, 8
	or	lr, r7, lr
	st.h	r10[0], lr
	.loc 1 6 0
	andl	r12, lo(-256)
	bfexts	r10, r11, 8, 16
.LVL2:
	bfextu	lr, r10, 0, 8
	or	r12, lr, r12
	st.h	r9[0], r12
	.loc 1 7 0
	andl	r10, lo(-256)
	or	r11, r10, r11 >> 24
.LVL3:
	st.h	r8[0], r11
	.loc 1 8 0
	ldm	sp++, r7, pc
.LFE0:
	.size	uballe, .-uballe
	.section	.text.sballe,"ax",@progbits
	.align	1
	.global	sballe
	.type	sballe, @function
sballe:
.LFB1:
	.loc 1 12 0
	# args = 0, frame = 0, pretend = 0
	# frame_needed = 0, leaf_function = 1
	# uses_anonymous_args = 0
.LVL4:
	stm	--sp, r7, lr
.LCFI1:
	.loc 1 13 0
	mov	lr, r12
	asr     r12, 16
.LVL5:
	andl	lr, lo(-256)
	bfextu	r7, r12, 0, 8
	or	lr, r7, lr
	st.h	r10[0], lr
	.loc 1 14 0
	andl	r12, lo(-256)
	bfexts	r10, r11, 8, 16
.LVL6:
	bfextu	lr, r10, 0, 8
	or	r12, lr, r12
	st.h	r9[0], r12
	.loc 1 15 0
	andl	r10, lo(-256)
	or	r11, r10, r11 >> 24
.LVL7:
	st.h	r8[0], r11
	.loc 1 16 0
	ldm	sp++, r7, pc
.LFE1:
	.size	sballe, .-sballe
	.section	.debug_frame,"",@progbits
.Lframe0:
	.int	.LECIE0-.LSCIE0
.LSCIE0:
	.int	0xffffffff
	.byte	0x1
	.string	""
	.uleb128 0x1
	.sleb128 -4
	.byte	0xe
	.byte	0xc
	.uleb128 0xd
	.uleb128 0x0
	.align	2
.LECIE0:
.LSFDE0:
	.int	.LEFDE0-.LASFDE0
.LASFDE0:
	.int	.Lframe0
	.int	.LFB0
	.int	.LFE0-.LFB0
	.byte	0x4
	.int	.LCFI0-.LFB0
	.byte	0xe
	.uleb128 0x8
	.byte	0x11
	.uleb128 0x7
	.sleb128 1
	.byte	0x11
	.uleb128 0xe
	.sleb128 2
	.align	2
.LEFDE0:
.LSFDE2:
	.int	.LEFDE2-.LASFDE2
.LASFDE2:
	.int	.Lframe0
	.int	.LFB1
	.int	.LFE1-.LFB1
	.byte	0x4
	.int	.LCFI1-.LFB1
	.byte	0xe
	.uleb128 0x8
	.byte	0x11
	.uleb128 0x7
	.sleb128 1
	.byte	0x11
	.uleb128 0xe
	.sleb128 2
	.align	2
.LEFDE2:
	.text
.Letext0:
	.section	.debug_loc,"",@progbits
.Ldebug_loc0:
.LLST0:
	.int	.LFB0
	.int	.LCFI0
	.short	0x1
	.byte	0x5d
	.int	.LCFI0
	.int	.LFE0
	.short	0x2
	.byte	0x7d
	.sleb128 8
	.int	0x0
	.int	0x0
.LLST1:
	.int	.LVL0
	.int	.LVL1
	.short	0x1
	.byte	0x5c
	.int	0x0
	.int	0x0
.LLST2:
	.int	.LVL0
	.int	.LVL3
	.short	0x1
	.byte	0x5b
	.int	0x0
	.int	0x0
.LLST3:
	.int	.LVL0
	.int	.LVL2
	.short	0x1
	.byte	0x5a
	.int	0x0
	.int	0x0
.LLST4:
	.int	.LFB1
	.int	.LCFI1
	.short	0x1
	.byte	0x5d
	.int	.LCFI1
	.int	.LFE1
	.short	0x2
	.byte	0x7d
	.sleb128 8
	.int	0x0
	.int	0x0
.LLST5:
	.int	.LVL4
	.int	.LVL5
	.short	0x1
	.byte	0x5c
	.int	0x0
	.int	0x0
.LLST6:
	.int	.LVL4
	.int	.LVL7
	.short	0x1
	.byte	0x5b
	.int	0x0
	.int	0x0
.LLST7:
	.int	.LVL4
	.int	.LVL6
	.short	0x1
	.byte	0x5a
	.int	0x0
	.int	0x0
	.file 2 "c:\\program files (x86)\\atmel\\avr tools\\avr toolchain\\bin\\../lib/gcc/avr32/4.4.7/../../../../avr32/include/stdint.h"
	.section	.debug_info
	.int	0x151
	.short	0x2
	.int	.Ldebug_abbrev0
	.byte	0x4
	.uleb128 0x1
	.int	.LASF14
	.byte	0x1
	.int	.LASF15
	.int	.LASF16
	.int	0x0
	.int	0x0
	.int	.Ldebug_ranges0+0x0
	.int	.Ldebug_line0
	.uleb128 0x2
	.byte	0x1
	.byte	0x6
	.int	.LASF0
	.uleb128 0x2
	.byte	0x1
	.byte	0x8
	.int	.LASF1
	.uleb128 0x2
	.byte	0x2
	.byte	0x5
	.int	.LASF2
	.uleb128 0x3
	.int	.LASF4
	.byte	0x2
	.byte	0x3a
	.int	0x49
	.uleb128 0x2
	.byte	0x2
	.byte	0x7
	.int	.LASF3
	.uleb128 0x3
	.int	.LASF5
	.byte	0x2
	.byte	0x53
	.int	0x5b
	.uleb128 0x2
	.byte	0x4
	.byte	0x5
	.int	.LASF6
	.uleb128 0x3
	.int	.LASF7
	.byte	0x2
	.byte	0x54
	.int	0x6d
	.uleb128 0x2
	.byte	0x4
	.byte	0x7
	.int	.LASF8
	.uleb128 0x2
	.byte	0x8
	.byte	0x5
	.int	.LASF9
	.uleb128 0x2
	.byte	0x8
	.byte	0x7
	.int	.LASF10
	.uleb128 0x4
	.byte	0x4
	.byte	0x5
	.string	"int"
	.uleb128 0x2
	.byte	0x4
	.byte	0x7
	.int	.LASF11
	.uleb128 0x5
	.byte	0x1
	.int	.LASF17
	.byte	0x1
	.byte	0x4
	.byte	0x1
	.int	.LFB0
	.int	.LFE0
	.int	.LLST0
	.int	0xf1
	.uleb128 0x6
	.int	.LASF12
	.byte	0x1
	.byte	0x4
	.int	0x62
	.int	.LLST1
	.uleb128 0x6
	.int	.LASF13
	.byte	0x1
	.byte	0x4
	.int	0x62
	.int	.LLST2
	.uleb128 0x7
	.string	"a16"
	.byte	0x1
	.byte	0x4
	.int	0xf1
	.int	.LLST3
	.uleb128 0x8
	.string	"b16"
	.byte	0x1
	.byte	0x4
	.int	0xf1
	.byte	0x1
	.byte	0x59
	.uleb128 0x8
	.string	"c16"
	.byte	0x1
	.byte	0x4
	.int	0xf1
	.byte	0x1
	.byte	0x58
	.byte	0x0
	.uleb128 0x9
	.byte	0x4
	.int	0x3e
	.uleb128 0xa
	.byte	0x1
	.int	.LASF18
	.byte	0x1
	.byte	0xc
	.byte	0x1
	.int	.LFB1
	.int	.LFE1
	.int	.LLST4
	.uleb128 0x6
	.int	.LASF12
	.byte	0x1
	.byte	0xc
	.int	0x50
	.int	.LLST5
	.uleb128 0x6
	.int	.LASF13
	.byte	0x1
	.byte	0xc
	.int	0x50
	.int	.LLST6
	.uleb128 0x7
	.string	"a16"
	.byte	0x1
	.byte	0xc
	.int	0xf1
	.int	.LLST7
	.uleb128 0x8
	.string	"b16"
	.byte	0x1
	.byte	0xc
	.int	0xf1
	.byte	0x1
	.byte	0x59
	.uleb128 0x8
	.string	"c16"
	.byte	0x1
	.byte	0xc
	.int	0xf1
	.byte	0x1
	.byte	0x58
	.byte	0x0
	.byte	0x0
	.section	.debug_abbrev
	.uleb128 0x1
	.uleb128 0x11
	.byte	0x1
	.uleb128 0x25
	.uleb128 0xe
	.uleb128 0x13
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x1b
	.uleb128 0xe
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x52
	.uleb128 0x1
	.uleb128 0x55
	.uleb128 0x6
	.uleb128 0x10
	.uleb128 0x6
	.byte	0x0
	.byte	0x0
	.uleb128 0x2
	.uleb128 0x24
	.byte	0x0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3e
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0xe
	.byte	0x0
	.byte	0x0
	.uleb128 0x3
	.uleb128 0x16
	.byte	0x0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0x0
	.byte	0x0
	.uleb128 0x4
	.uleb128 0x24
	.byte	0x0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3e
	.uleb128 0xb
	.uleb128 0x3
	.uleb128 0x8
	.byte	0x0
	.byte	0x0
	.uleb128 0x5
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0xc
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0xc
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x1
	.uleb128 0x40
	.uleb128 0x6
	.uleb128 0x1
	.uleb128 0x13
	.byte	0x0
	.byte	0x0
	.uleb128 0x6
	.uleb128 0x5
	.byte	0x0
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x6
	.byte	0x0
	.byte	0x0
	.uleb128 0x7
	.uleb128 0x5
	.byte	0x0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0x6
	.byte	0x0
	.byte	0x0
	.uleb128 0x8
	.uleb128 0x5
	.byte	0x0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0xa
	.byte	0x0
	.byte	0x0
	.uleb128 0x9
	.uleb128 0xf
	.byte	0x0
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.byte	0x0
	.byte	0x0
	.uleb128 0xa
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x3f
	.uleb128 0xc
	.uleb128 0x3
	.uleb128 0xe
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0xc
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x1
	.uleb128 0x40
	.uleb128 0x6
	.byte	0x0
	.byte	0x0
	.byte	0x0
	.section	.debug_pubnames,"",@progbits
	.int	0x24
	.short	0x2
	.int	.Ldebug_info0
	.int	0x155
	.int	0x90
	.string	"uballe"
	.int	0xf7
	.string	"sballe"
	.int	0x0
	.section	.debug_aranges,"",@progbits
	.int	0x24
	.short	0x2
	.int	.Ldebug_info0
	.byte	0x4
	.byte	0x0
	.short	0x0
	.short	0x0
	.int	.LFB0
	.int	.LFE0-.LFB0
	.int	.LFB1
	.int	.LFE1-.LFB1
	.int	0x0
	.int	0x0
	.section	.debug_ranges,"",@progbits
.Ldebug_ranges0:
	.int	.Ltext0
	.int	.Letext0
	.int	.LFB0
	.int	.LFE0
	.int	.LFB1
	.int	.LFE1
	.int	0x0
	.int	0x0
	.section	.debug_str,"MS",@progbits,1
.LASF15:
	.string	"sample.c"
.LASF17:
	.string	"uballe"
.LASF1:
	.string	"unsigned char"
.LASF8:
	.string	"long unsigned int"
.LASF3:
	.string	"short unsigned int"
.LASF18:
	.string	"sballe"
.LASF16:
	.string	"E:\\kitten\\avr32\\github\\sdr-widget\\src"
.LASF11:
	.string	"unsigned int"
.LASF10:
	.string	"long long unsigned int"
.LASF13:
	.string	"sample_right"
.LASF5:
	.string	"int32_t"
.LASF9:
	.string	"long long int"
.LASF12:
	.string	"sample_left"
.LASF14:
	.string	"GNU C 4.4.7"
.LASF2:
	.string	"short int"
.LASF4:
	.string	"uint16_t"
.LASF7:
	.string	"uint32_t"
.LASF6:
	.string	"long int"
.LASF0:
	.string	"signed char"
	.ident	"GCC: (AVR_32_bit_GNU_Toolchain_3.4.2_435) 4.4.7"

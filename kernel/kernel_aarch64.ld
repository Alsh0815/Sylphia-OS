ENTRY(KernelMain)

SECTIONS
{
    /* カーネルのロード位置。AArch64 UEFIでは任意のアドレスにロードされる可能性があるが、
       今回はPIEとして生成するため、相対アドレスで記述する。
       ただし、初期ページテーブルの設定を簡略化するため、仮想アドレスのベースを合わせておく。 */
    . = 0xffffff8040000000; /* 上位アドレス(カーネル空間)にマッピングする場合の例 */
    /* あるいは物理アドレス 1:1 マッピングを想定して 0x40000000 から開始するなど */
    
    /* UEFI環境下でのロードを考慮し、0x0 からの相対オフセットとして記述するのが一般的だが、
       既存のkernel.ldに合わせて単純な構造にする。 */
    
    /* AArch64ではPIE生成時に . = 0; から始めるのが一般的 */
     . = 0;

    __kernel_start = .;

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        . = ALIGN(0x1000);
        *(.data .data.*)
    }

    .bss : {
        . = ALIGN(0x1000);
        *(.bss .bss.*)
        *(COMMON)
    }
    
    /* 動的リンクに必要なセクション (PIE用) */
    .rela.dyn : {
        *(.rela*)
    }
    
    .dynamic : {
        *(.dynamic)
    }

    .got : {
        *(.got)
    }
    
    .got.plt : {
        *(.got.plt)
    }

    __kernel_end = .;
}

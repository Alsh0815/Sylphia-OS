ENTRY(KernelMain)

SECTIONS
{
    /* カーネルのロード位置を 0x100000 に設定 */
    . = 0x100000;
    __kernel_start = .;

    /* .text セクション (プログラムコード) */
    .text : {
        *(.text)
    }

    /* .rodata セクション (定数データ) */
    .rodata : {
        *(.rodata)
    }

    /* .data セクション (初期値あり変数) */
    .data : {
        *(.data)
    }

    /* .bss セクション (初期値なし変数) */
    .bss : {
        *(.bss)
        *(COMMON)
    }

    __kernel_end = .;
}
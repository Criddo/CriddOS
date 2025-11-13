ENTRY(start)

SECTIONS
{
	. = 0x100000;

	.multiboot_header ALIGN(8) :
	{
		*(.multiboot_header)
	}

	.text ALIGN(8) :
	{
		*(.text)
	}

	.bss ALIGN(8) :
	{
		*(.bss)
	}

	.rodata ALIGN(8) :
	{
		*(.rodata)
	}

}


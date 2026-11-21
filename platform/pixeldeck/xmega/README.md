# xmega-Firmware der Konsole

Aus `larsledmatrix/Software/platforms/HMatrix/xmega` übernommen (Stand 2025-05 plus pixeldeck M3:
200-Hz-Statuspakete, Power-Taste per Polling mit Gnadenfrist). LUFA liegt als Teilmenge unter
`lufa/LUFA` (Version 170418, Lizenz in `lufa/LUFA/License.txt`). Protokoll: `docs/SPEC.md` 1.2.

## Setup

	sudo apt install gcc-avr
	sudo apt install avr-libc
	sudo apt install dfu-programmer

## Build

	make

## Bootloader
Required: set fuses: (Enable Watchdog and Brownout Detection)

	avrdude -c avrisp2 -p x128a4u -U fuse1:w:0x00:m
	avrdude -c avrisp2 -p x128a4u -U fuse2:w:0xbd:m
	avrdude -c avrisp2 -p x128a4u -U fuse4:w:0xfa:m
	avrdude -c avrisp2 -p x128a4u -U fuse5:w:0xe8:m
Optional: install bootloader which will make it USB flashable

	avrdude -c avrisp2 -p x128a4u -U flash:w:bootloader.hex:i

## Flash
If bootloader is installed:

	dfu-programmer atxmega128a4u erase
	dfu-programmer atxmega128a4u flash LaLeMa.hex
	dfu-programmer atxmega128a4u start
Otherwise:

	avrdude -c avrisp2 -p x128a4u -U flash:w:LaLeMa.hex

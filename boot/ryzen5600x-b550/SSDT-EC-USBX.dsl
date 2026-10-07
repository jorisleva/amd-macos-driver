/* Adapted from OpenCorePkg 1.0.8 Docs/AcpiSamples/Source/SSDT-EC-USBX.dsl.
 * B550M DS3H BIOS FD: DSDT SHA256 4a382ee7c5311f768b99dd2968d8808010246658d3a21824dd90ed788c3fdb8c.
 * LPC bridge is _SB.PCI0.SBRG. No PNP0C09 EC or USBX in the captured DSDT.
 * Fake EC does not load AppleACPIEC; both devices exist only for Darwin.
 * Firmware SSDT capture via Windows is incomplete; actual boot still needs validation.
 */
DefinitionBlock ("", "SSDT", 2, "JORIS", "ECUSBX", 0x00000001)
{
    External (_SB_.PCI0.SBRG, DeviceObj)
    Scope (\_SB.PCI0.SBRG)
    {
        Device (EC)
        {
            Name (_HID, "ACID0001")
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin")) { Return (0x0F) }
                Return (Zero)
            }
        }
    }
    Scope (\_SB)
    {
        Device (USBX)
        {
            Name (_ADR, Zero)
            Method (_STA, 0, NotSerialized)
            {
                If (_OSI ("Darwin")) { Return (0x0F) }
                Return (Zero)
            }
            Method (_DSM, 4, NotSerialized)
            {
                If (Arg2 == Zero) { Return (Buffer (One) { 0x03 }) }
                Return (Package (0x08) {
                    "kUSBSleepPowerSupply", 0x13EC,
                    "kUSBSleepPortCurrentLimit", 0x0834,
                    "kUSBWakePowerSupply", 0x13EC,
                    "kUSBWakePortCurrentLimit", 0x0834
                })
            }
        }
    }
}

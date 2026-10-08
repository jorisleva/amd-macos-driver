/* EC-only variant of SSDT-EC-USBX.dsl for the native USB installation trial.
 * B550M DS3H BIOS FD: observed LPC bridge is _SB.PCI0.SBRG.
 * Retains the same fake EC; no USBX power properties or USB port definitions.
 */
DefinitionBlock ("", "SSDT", 2, "JORIS", "ECONLY", 0x00000001)
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
}

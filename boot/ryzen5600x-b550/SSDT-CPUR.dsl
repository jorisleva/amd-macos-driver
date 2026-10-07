/* Based on Dortania SSDT-CPUR; restricted to the 12 observed logical CPU paths.
 * Source provenance and original source checksum: sources.lock.json.
 * Windows PnP reports _SB.PLTF.C000 through C00B on BIOS FD.
 */
DefinitionBlock ("", "SSDT", 2, "JORIS", "CPUR", 0x00000001)
{
    External (_SB_.PLTF.C000, DeviceObj)
    External (_SB_.PLTF.C001, DeviceObj)
    External (_SB_.PLTF.C002, DeviceObj)
    External (_SB_.PLTF.C003, DeviceObj)
    External (_SB_.PLTF.C004, DeviceObj)
    External (_SB_.PLTF.C005, DeviceObj)
    External (_SB_.PLTF.C006, DeviceObj)
    External (_SB_.PLTF.C007, DeviceObj)
    External (_SB_.PLTF.C008, DeviceObj)
    External (_SB_.PLTF.C009, DeviceObj)
    External (_SB_.PLTF.C00A, DeviceObj)
    External (_SB_.PLTF.C00B, DeviceObj)
    If (_OSI ("Darwin"))
    {
        Scope (\_SB)
        {
            Processor (PR00, 0x00, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C000)
            }
            Processor (PR01, 0x01, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C001)
            }
            Processor (PR02, 0x02, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C002)
            }
            Processor (PR03, 0x03, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C003)
            }
            Processor (PR04, 0x04, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C004)
            }
            Processor (PR05, 0x05, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C005)
            }
            Processor (PR06, 0x06, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C006)
            }
            Processor (PR07, 0x07, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C007)
            }
            Processor (PR08, 0x08, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C008)
            }
            Processor (PR09, 0x09, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C009)
            }
            Processor (PR10, 0x0A, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C00A)
            }
            Processor (PR11, 0x0B, 0x00000810, 0x06)
            {
                Return (\_SB.PLTF.C00B)
            }
        }
    }
}

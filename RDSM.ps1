# Define the Win32 API signature to safely grab UNC network share space
$Signature = @'
using System;
using System.Runtime.InteropServices;

public class NetworkDriveHelper {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetDiskFreeSpaceEx(
        string lpDirectoryName,
        out ulong lpFreeBytesAvailable,
        out ulong lpTotalNumberOfBytes,
        out ulong lpTotalNumberOfFreeBytes
    );
}
'@

# Add the type definition to the PowerShell session if it doesn't already exist
if (-not ([System.Management.Automation.PSTypeName]'NetworkDriveHelper').Type) {
    Add-Type -TypeDefinition $Signature
}

# UPDATE THIS LIST WITH YOUR ACTUAL TARGETS
$Shares = @(
    "\\Chip\work",
    "\\Dale\work",
    "\\bmanske-plex-pc\Plex-I",
    "\\bmanske-plex-pc\Plex-J",
    "\\bmanske-plex-pc\Plex-K",
    "\\bmanske-game-pc\Game-D"
)

Write-Host "Fetching remote drive space info..." -ForegroundColor Cyan
Write-Host "--------------------------------------------------"

foreach ($Share in $Shares) {
    # Ensure the string ends with a trailing backslash which the Win32 API prefers
    $TargetPath = $Share
    if (-not $TargetPath.EndsWith("\")) { $TargetPath += "\" }

    # Initialize empty variables (PowerShell handles the reference types automatically)
    $FreeBytesAvailable = 0
    $TotalNumberOfBytes = 0
    $TotalNumberOfFreeBytes = 0

    # Call the native API directly
    $Success = [NetworkDriveHelper]::GetDiskFreeSpaceEx($TargetPath, [ref]$FreeBytesAvailable, [ref]$TotalNumberOfBytes, [ref]$TotalNumberOfFreeBytes)

    if ($Success) {
        $TotalSizeGB = [Math]::Round($TotalNumberOfBytes / 1GB, 2)
        $FreeSpaceGB = [Math]::Round($FreeBytesAvailable / 1GB, 2)
        $UsedSpaceGB = [Math]::Round(($TotalNumberOfBytes - $FreeBytesAvailable) / 1GB, 2)
        
        # Guard against division by zero if it's an empty or uninitialized mount
        $PercentFree = 0
        if ($TotalSizeGB -gt 0) {
            $PercentFree = [Math]::Round(($FreeSpaceGB / $TotalSizeGB) * 100, 1)
        }

        Write-Host "Share: $Share" -ForegroundColor Green
        Write-Host "  Total Size:  $TotalSizeGB GB"
        Write-Host "  Used Space:  $UsedSpaceGB GB"
        Write-Host "  Free Space:  $FreeSpaceGB GB ($PercentFree% Free)"
        Write-Host "--------------------------------------------------"
    }
    else {
        Write-Host "Share: $Share -> [OFFLINE, PATH INVALID, or ACCESS DENIED]" -ForegroundColor Red
        Write-Host "--------------------------------------------------"
    }
}

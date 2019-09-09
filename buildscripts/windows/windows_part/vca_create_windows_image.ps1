<#
.NOTES
    Intel VCA Software Stack (VCASS)

    Copyright(c) 2016 Intel Corporation.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License, version 2, as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    The full GNU General Public License is included in this distribution in
    the file called "COPYING".

    Intel VCA Scripts.

.SYNOPSIS
    Creates a bootable ISO image based on Windows installation media to use with VCA BlockIO & KVM or Xen.

.DESCRIPTION
    Creates a bootable ISO image based on Windows installation media to use with VCA BlockIO & KVM or Xen.
#>

param(
    [string]$iso,
    [string]$output_dir = (Join-Path $pwd.path -ChildPath 'out'),
    [string]$mount_dir = (Join-Path $pwd.path -ChildPath 'mount_dir'),
    [string]$tmp_dir = (Join-Path $pwd.path -ChildPath 'tmp'),
    [string]$driver_dir,
    [string]$vcagent_dir,
    [string]$answer_file,
    [string]$dism_path = $(Join-Path (Get-item env:\windir).value "system32\dism.exe"),
    [string]$vca_image_version = '0.0.0',
    [string]$gfx_drv_dir,
    [string]$win_edition,
    [string]$netkvm_drv_dir,
    [string]$virtualbox,
    [string]$xen_driver_dir,
    [string]$openssh_dir,
    [string]$boot_part_type = 'GPT',
    [string]$zip_img = 'vca_windows_baremetal.zip',
    [long]$vhd_size = 32GB
)

# variable used to indicate if GFX/KVM/Xen driver installation should be attempted
[Bool]$install_gfx_drv_status = 0
[Bool]$install_xen_drv_status = 0
[Bool]$install_netkvm_drv_status = 0
$LASTEXITCODE = 0
$ErrorActionPreference = "Stop"

if (-Not $iso) { Write-Error "Missing -iso parameter"; Exit 1 }
if (-Not $answer_file) { Write-Error "Missing -answer_file parameter"; Exit 1 }
if (-Not $driver_dir) { Write-Error "Missing -driver_dir parameter"; Exit 1 }
if (-Not $vcagent_dir) { Write-Error "Missing -vcagent_dir parameter"; Exit 1 }
if (-Not $win_edition) { Write-Error "Missing -win_edition parameter"; Exit 1 }
if (-Not $virtualbox) { Write-Error "Missing -virtualbox parameter"; Exit 1 }
if (-Not $zip_img) {Write-Error "Missing -zip_img parameter" Exit 1}
if ($gfx_drv_dir) {$install_gfx_drv_status = 1}
if ($xen_driver_dir) {$install_xen_drv_status = 1}
if ($openssh_dir) {$install_openssh_status = 1}
if ($netkvm_drv_dir) {$install_netkvm_drv_status = 1}

# create default directories if they do not exist
if (-Not (Test-Path $output_dir))
{
    New-Item $output_dir -type directory
    Write-Host "Output directory created: " $output_dir
}

if (-Not (Test-Path $mount_dir))
{
    New-Item $mount_dir -type directory
    Write-Host "Mount directory created: " $mount_dir
}

if (-Not (Test-Path $tmp_dir))
{
    New-Item $tmp_dir -type directory
    Write-Host "Tmp directory created: " $tmp_dir
}

# check each parameter if exist, not only if was provided by user
if(-Not (Test-Path $iso)) { Write-Error "ISO file" $iso "does not exist"; Exit 1}
if(-Not (Test-Path $answer_file)) { Write-Error "Answer file" $answer_file "does not exist"; Exit 1 }
if(-Not (Test-Path $vcagent_dir)) { Write-Error "VCAgent directory" $vcagent_dir "does not exist"; Exit 1 }
if(-Not (Test-Path $driver_dir)) { Write-Error "Driver directory" $driver_dir "does not exist"; Exit 1 }
if ($gfx_drv_dir -ne "")
{
    if(-Not (Test-Path $gfx_drv_dir)) { Write-Error "GFX driver directory " $gfx_drv_dir " does not exist"; Exit 1 }
}
if ($xen_driver_dir -ne "")
{
    if(-Not (Test-Path $xen_driver_dir)) { Write-Error "Xen driver directory " $xen_driver_dir " does not exist"; Exit 1 }
}
if ($netkvm_drv_dir -ne "")
{
    if(-Not (Test-Path $netkvm_drv_dir)) { Write-Error "NetKVM driver directory " $netkvm_drv_dir " does not exist"; Exit 1 }
}
if ($openssh_dir -ne "")
{
    if(-Not (Test-Path $openssh_dir)) { Write-Error "OpenSSH driver directory " $openssh_dir " does not exist"; Exit 1 }
}

$driver_vca = Join-Path $driver_dir -ChildPath 'VCA Package\Vca.inf'
if(-Not (Test-Path $driver_vca)) { Write-Error "VCA driver's .inf file not exist"; Exit 1 }

$driver_blk = Join-Path $driver_dir -ChildPath 'VcaBlockIo Package\VcaBlockIo.inf'
if(-Not (Test-Path $driver_blk)) { Write-Error "VcaBlockIo driver's .inf file not exist"; Exit 1 }

$driver_veth = Join-Path $driver_dir -ChildPath 'VcaVeth Package\VcaVeth.inf'
if(-Not (Test-Path $driver_veth)) { Write-Error "VcaVeth driver's .inf file not exist"; Exit 1 }

if ($netkvm_drv_dir -ne "")
{
    $driver_netkvm = Join-Path $netkvm_drv_dir -ChildPath 'amd64\netkvm.inf'
    if(-Not (Test-Path $driver_netkvm)) { Write-Error "NetKVM driver's .inf file not exist"; Exit 1 }
}

if ($xen_driver_dir -ne "")
{
    $driver_xenbus = Join-Path $xen_driver_dir -ChildPath 'xenbus\x64\xenbus.inf'
    if(-Not (Test-Path $driver_xenbus)) { Write-Error "XenBus driver's .inf file not exist"; Exit 1 }

    $driver_xennet = Join-Path $xen_driver_dir -ChildPath 'xennet\x64\xennet.inf'
    if(-Not (Test-Path $driver_xenbus)) { Write-Error "XenNet driver's .inf file not exist"; Exit 1 }

    $driver_xenvif = Join-Path $xen_driver_dir -ChildPath 'xenvif\x64\xenvif.inf'
    if(-Not (Test-Path $driver_xenbus)) { Write-Error "Xenvif driver's .inf file not exist"; Exit 1 }
}


$workdir = $pwd.path
$output_name = 'windows_image.vhd'
$output_iso_name = 'windows_image_@_version.img'
$output_full_path = Join-Path $output_dir -ChildPath $output_name
$output_iso_full_path = Join-Path $output_dir -ChildPath $output_iso_name

function DisplayInGBytes($num)
{
    $num = $num / 1GB
    Write-Host $num"GB"
}


function SummarizeParameters
{
    Write-Host ""
    Write-Host "Working Directory: " $workdir
    Write-Host "Setup script: " $setup_script
    Write-Host "Windows ISO: " $iso
    Write-Host "Output dir: " $output_dir
    Write-Host "Output vhd name: " $output_name
    Write-Host "Mount dir: " $mount_dir
    Write-Host "VcaWinKmd drivers path: " $driver_dir
    Write-Host "driver_vca path: " $driver_vca
    Write-Host "driver_blk path: " $driver_blk
    Write-Host "driver_veth path: " $driver_veth
    Write-Host "VCAgent path: " $vcagent_dir
    Write-Host "Answer file path: " $answer_file
    Write-Host "Dism.exe path: " $dism_path
    Write-Host "VirtualBox path: " $virtualbox
    Write-Host -NoNewline "VHD size: "
        DisplayInGBytes $vhd_size

    if ($gfx_drv_dir -ne "")
    {
        Write-Host "GFX driver path: " $gfx_drv_dir
    }

    if ($netkvm_drv_dir -ne "")
    {
        Write-Host "NetKVM driver path: " $driver_netkvm
    }

    if ($xen_driver_dir -ne "")
    {
        Write-Host "XenBus driver path: " $driver_xenbus
        Write-Host "XenNet driver path: " $driver_xennet
        Write-Host "XenVif driver path: " $driver_xenvif
    }

    Write-Host ""
}

function CopyAndRenameCatalogFromTo
{
    $from = $args[0]
    $to = $args[1]
    $newname = $args[2]

    $returned_object = Copy-Item $from $to -recurse -PassThru # if force added then override, if not then ask about confirmation
    if ($returned_object)
    {
        $tempname_dir = Join-Path $to -ChildPath (Split-Path $from -Leaf)
        if ($tempname_dir -ne (Join-Path $to -ChildPath $newname))
        {
            $returned_object = Rename-Item $tempname_dir $newname -PassThru
            if (-Not $returned_object)
                {
                Write-Error "`nCannot rename $tempname_dir directory to $newname"
                Return 1
                }
        }
        Write-Host "`n $from copied to : " (Join-Path $to -ChildPath $newname)
        Return 0
    }

    Write-Error "`nCannot copy $from directory to " (Join-Path $to -ChildPath $newname)
    Return 1
}

function GenerateImage
{
 
   
    $newVhd = New-VHD -Path $output_full_path -Dynamic -SizeBytes $vhd_size
    if (-Not $newVhd)
          {
           Write-Error "`nCannot create $newVhd file"
           Return 1
          }
   
    Write-Host "`nAttaching VHD file..."
    Mount-VHD –Path $output_full_path

    $newVhd = get-vhd -path $output_full_path -Verbose
    
    if ($boot_part_type -eq "MBR" ) {

        Initialize-Disk -Number $newVhd.DiskNumber -PartitionStyle MBR

        Write-Host "`nWe are partitioning VHD with MBR..."    

        $disk_no = Get-Disk -Number $newVhd.DiskNumber

        $partition       = New-Partition -DiskNumber $newVhd.DiskNumber -Size $disk_no.LargestFreeExtent -MbrType IFS -IsActive
                        Write-Host "`nVHD disk has been partitioned..."

        $volume    = Format-Volume -Partition $partition -Force -Confirm:$false -FileSystem NTFS
                        Write-Host "`nVHD system volume has been formatted..."

        $partition       | Add-PartitionAccessPath -AssignDriveLetter
        $drive           = $(Get-Partition -Disk $disk_no).AccessPaths[0]
        
        Write-Host "`nDrive letter ($drive) for system volume has been assigned..."



    } elseif ($boot_part_type -eq "GPT" ) {
        Initialize-Disk -Number $newVhd.DiskNumber -PartitionStyle GPT
        
        Write-Host "`nWe are partitioning VHD with GPT..."

        $disk_no = Get-Disk -Number $newVhd.DiskNumber

        $WindowsPartition = New-Partition -DiskNumber $newVhd.DiskNumber -Size 100MB -GptType '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'
                            Write-Host "`nVHD's Windows partition has been created"

        $partition       = New-Partition -DiskNumber $newVhd.DiskNumber -UseMaximumSize -GptType '{ebd0a0a2-b9e5-4433-87c0-68b6b72699c7}'
                            Write-Host "`nVHD's boot partition has been created"

        @"
        select disk $($disk_no.Number)
        select partition $($WindowsPartition.PartitionNumber)
        format fs=fat32 label="System"
"@ | & $env:SystemRoot\System32\DiskPart.exe | Out-Null
        
        $volume          = Format-Volume -Partition $partition -FileSystem NTFS -Force -Confirm:$false
                        Write-Host "Boot Volume formatted (with Format-Volume)..."

        $WindowsPartition | Add-PartitionAccessPath -AssignDriveLetter

        $WindowsDrive     = $(Get-Partition -Disk $disk_no).AccessPaths[1]

        Write-Host "Access path ($WindowsDrive) has been assigned to the Windows System Volume..."

        New-Item $WindowsDrive\startup.nsh -type file -force -value "fs0:\EFI\Boot\bootx64.efi"

        $partition       | Add-PartitionAccessPath -AssignDriveLetter

        $drive           = $(Get-Partition -Disk $disk_no).AccessPaths[2]

        Write-Host "`nAccess path ($drive) has been assigned to the Boot Volume..."


      }

    #-----------------------------------------------------------------------------------
    #---------------------------Copying WIM image from install ISO ---------------------
    #-----------------------------------------------------------------------------------

    $iso_full = Resolve-Path -Path "$iso"

    Write-host "`nMounting Windows installation ISO ($iso)"

    Mount-DiskImage -ImagePath $iso_full -StorageType ISO -PassThru

    $iso_mount_info = get-diskimage -imagepath  $iso_full

    $iso_drive_letter = ($iso_mount_info | Get-Volume).DriveLetter

    $wim_image = "$($iso_drive_letter):\sources\install.wim"

    Write-host "`nCopying WIM file from Windows installation ISO"

    Copy-Item -Path $wim_image -Destination $tmp_dir -Force

    Write-host "`nDismounting ISO"
    Dismount-DiskImage -ImagePath $iso_full -StorageType ISO -PassThru

    $wim_image = Join-Path $tmp_dir -ChildPath 'install.wim'
    set-itemproperty $wim_image isreadonly $false

    Write-host "`nMounting WIM"

    #-----------------------------------------------------------------------------------
    #---------------------------Mounting WIM image to mount directory ------------------
    #-----------------------------------------------------------------------------------

    $wim_image_info = Get-WindowsImage -ImagePath $wim_image | Where-Object {$_.ImageName -ilike "*$($win_edition)"}
    $img_index = $wim_image_info[0].ImageIndex
    Write-Host "`nMounting WIM image=$wim_image, index=$img_index, mount_dir=$mount_dir"
    $process = Start-Process -PassThru -NoNewWindow -FilePath $dism_path `
                        -ArgumentList /Mount-wim, /wimfile:"$wim_image", /index:"$img_index", /MountDir:"$mount_dir"
    $process.WaitForExit()
    Write-Host $process.ExitCode
    if ($process.ExitCode)
    {
        Start-Process -FilePath $dism_path -ArgumentList /Unmount-wim, /MountDir:`"$mount_dir`", /discard
        Write-Error "`nMounting $wim_image failed! Unmounting directory $mount_dir"
        Return $process.ExitCode
    }

    Write-Host "`nMounting WIM done."

    $StopWatch.Elapsed

    #-----------------------------------------------------------------------------------
    #---------------------------Applying drivers to WIM file ---------------------------
    #-----------------------------------------------------------------------------------

       $process = Start-Process -FilePath $dism_path -Wait -NoNewWindow -PassThru `
                    -ArgumentList /Image:`"$mount_dir`",  /Add-Driver, /Driver:`"$driver_vca`", /forceunsigned

    if ($process.ExitCode -ne 0)
    {
        Start-Process -FilePath $dism_path -ArgumentList /Unmount-wim, /MountDir:`"$mount_dir`", /discard
        Write-Error "`nInstalling driver $driver_vca failed! See Dism log for details"
        Return $process.ExitCode
    }

    $process = Start-Process -FilePath $dism_path -Wait -NoNewWindow -PassThru `
                    -ArgumentList /Image:`"$mount_dir`",  /Add-Driver, /Driver:`"$driver_blk`", /forceunsigned

    if ($process.ExitCode -ne 0)
    {
        Write-Error "`nInstalling driver $driver_blk failed! See Dism log for details"
        Return $process.ExitCode
    }

    $process = Start-Process -FilePath $dism_path -Wait -NoNewWindow -PassThru `
                    -ArgumentList /Image:`"$mount_dir`",  /Add-Driver, /Driver:`"$driver_veth`", /forceunsigned

    if ($process.ExitCode -ne 0)
    {
        Write-Error "`nInstalling driver $driver_veth failed! See Dism log for details"
        Return $process.ExitCode
    }

    Write-Host "`nInstalling VCA drivers done"

    #-----------------------------------------------------------------------------------
    #-----------------------Installing KVM driver to WIM file -------------------------
    #-----------------------------------------------------------------------------------

    if ($netkvm_drv_dir -ne "")
    {
        Write-Host "`nInstalling KVM driver"
        $process = Start-Process -FilePath $dism_path -Wait -NoNewWindow -PassThru `
                     -ArgumentList /Image:`"$mount_dir`",  /Add-Driver, /Driver:`"$driver_netkvm`", /forceunsigned

        if ($process.ExitCode -ne 0)
        {
            Write-Error "`nInstalling driver $driver_netkvm failed! See Dism log for details"
            Return $process.ExitCode
        }
        Write-Host "`nInstalling KVM driver done"
    }

    #-----------------------------------------------------------------------------------
    #--------Copying answerfile / GFX driver / Xen drivers to WIM file -----------------
    #-----------------------------------------------------------------------------------

    $dest_answer_file = Join-Path $mount_dir -ChildPath 'unattend.xml'
    Copy-Item -Path $answer_file -Destination $dest_answer_file -Force

    $workfiles_dir = Join-Path $mount_dir -ChildPath 'VCA'
    if (-Not (Test-Path $workfiles_dir))
    {
        New-Item $workfiles_dir -type directory
        Write-Host "`nVCA directory created: " $workfiles_dir
    }

    if ( $status -eq 0 -And $install_gfx_drv_status -eq 1)
    {
    	$rc = CopyAndRenameCatalogFromTo $gfx_drv_dir $workfiles_dir GFX_driver
    	if ($rc -ne 0)
    	{
		    $status = 2
	    }
    }

    if ($status -eq 0)
    {
        $rc = ((CopyAndRenameCatalogFromTo $vcagent_dir $workfiles_dir VCAgent)[-1])
        if ($rc -ne 0)
        {
            $status = 3
        }
    }

    if ( $status -eq 0 -And $install_openssh_status -eq 1)
    {
    	$rc = CopyAndRenameCatalogFromTo $openssh_dir $workfiles_dir OpenSSH
    	if ($rc -ne 0)
    	{
		    $status = 4
	    }
    }

    if ( $status -eq 0 -And $install_xen_drv_status -eq 1)
    {
    	$rc = CopyAndRenameCatalogFromTo $xen_driver_dir $workfiles_dir Xen
    	if ($rc -ne 0)
    	{
		    $status = 5
	    }
    }

    $process  = Start-Process -Passthru -Wait -NoNewWindow -FilePath $dism_path `
                            -ArgumentList /Unmount-wim, /MountDir:`"$mount_dir`", /Commit
    if ($process.ExitCode -ne 0)
    {
        Write-Error "`nUnmounting WIM $wim_image failed!"
        Return $process.ExitCode
    }

    Write-Host "`nUnmounting WIM $wim_image done."

    #-----------------------------------------------------------------------------------
    #---------------------------Applying WIM image with drivers to VHD------------------
    #-----------------------------------------------------------------------------------

    $StopWatch.Elapsed
    
    $wim_image = Join-Path $tmp_dir -ChildPath 'install.wim'
    
    Write-Host "`nWIM image: " $wim_image

    Write-Host "`nApplying WIM to VHD"
    
    $wim_image_info = Get-WindowsImage -ImagePath $wim_image | Where-Object {$_.ImageName -ilike "*$($win_edition)"}
    
    $img_index = $wim_image_info[0].ImageIndex

    Write-Host $wim_image_info[0].ImageName
    Write-Host $wim_image_info[0].ImagePath
    Write-Host $wim_image_info[0].ImageIndex
    
    $process = Start-Process -FilePath $dism_path -Wait -NoNewWindow -PassThru `
                    -ArgumentList /Apply-Image, /Imagefile:$wim_image,  /index:$img_index, /ApplyDir:$drive
    do {start-sleep -Milliseconds 500}
      until ($process.HasExited)

    if ($LASTEXITCODE -ne 0)
        {
            Write-Error "`nApplying WIM failed! See Dism log file for details"
            Return $process.ExitCode
        }
    
    Write-Host "`nDeleting temporary WIM file"
    del $wim_image

    #-----------------------------------------------------------------------------------
    #----------------------------------Creating Boot files------------------------------
    #-----------------------------------------------------------------------------------


    if ($boot_part_type -eq "MBR" ) {

        $process = Start-Process -RedirectStandardOutput null -Passthru -Wait -NoNewWindow -FilePath "bcdboot.exe"`
        -ArgumentList @("$($drive)Windows", "/f BIOS", "/v", "/s $drive")
        
        if ($process.ExitCode -ne 0)
        {
            Write-Error "`nCan't create boot files on ($drive)"
            Return $process.ExitCode
        }

        $process = Start-Process -RedirectStandardOutput null -WindowStyle Hidden -Passthru -Wait -FilePath "bcdedit.exe"`
        -ArgumentList @("/store $($drive)boot\bcd", "/set `{bootmgr`} device locate")

        if ($process.ExitCode -ne 0)
        {
            Write-Error "`nCan't modify BCD on " $drive
            Return $process.ExitCode
        }

        $process = Start-Process -RedirectStandardOutput null -WindowStyle Hidden -Passthru -Wait -FilePath "bcdedit.exe"`
        -ArgumentList @("/store $($drive)boot\bcd", "/set `{default`} osdevice locate")

        if ($process.ExitCode -ne 0)
        {
            Write-Error "`nCan't modify BCD on " $drive
            Return $process.ExitCode
        }


    } elseif ($boot_part_type -eq "GPT" ) {
        
        $process = Start-Process -RedirectStandardOutput null -WindowStyle Hidden -Passthru -Wait -FilePath "bcdboot.exe"`
        -ArgumentList @("$($drive)Windows", "/f UEFI", "/v", "/s $WindowsDrive")

        if ($process.ExitCode -ne 0)
        {
            Write-Error "`nCan't create UEFI boot files on " $WindowsDrive
            Return $process.ExitCode
        }

    }
    
    Write-Host "`nClosing VHD file..."
    dismount-vhd -path $output_full_path
   
   Return 0
}


function CreateIsoFromVhd
{
    Write-Host "`nConverting VHD to IMG"
    
    $process = Start-Process -FilePath $virtualbox -Wait -NoNewWindow -PassThru `
                    -ArgumentList clonehd, `"$output_full_path`", `"$output_iso_full_path`", --format, RAW

    if ($process.ExitCode -ne 0)
    {
        Write-Error "`nConversion of " $output_full_path " to " $output_iso_full_path " failed! See Dism log for details"
        Return $process.ExitCode
    }

    del $output_full_path

    $img_new_name_prefix_t = $zip_img -split 'baremetal'
    $img_new_name_prefix = $img_new_name_prefix_t[0] + "baremetal"

    $img_new_name = $output_iso_full_path.Replace("version", "$vca_image_version")
    $img_new_name = $img_new_name.Replace("@", "$boot_part_type")
    $img_new_name = $img_new_name.Replace("windows_image", "$img_new_name_prefix")
    $returned_object = Rename-Item -Path $output_iso_full_path -NewName $img_new_name -PassThru
    if (-Not $returned_object)
    {
        Write-Error "`nCannot rename " $output_iso_full_path " to " $img_new_name
        Return 1
    }

    Write-Host "`nConversion of VHD to IMG done"
    Return $process.ExitCode
}

function CopyAndRenameCatalogFromTo
{
$from = $args[0]
$to = $args[1]
$newname = $args[2]

 $returned_object = Copy-Item $from $to -recurse -PassThru # if force added then override, if not then ask about confirmation
    if ($returned_object)
    {
        $tempname_dir = Join-Path $to -ChildPath (Split-Path $from -Leaf)
        if ($tempname_dir -ne (Join-Path $to -ChildPath $newname))
        {
            $returned_object = Rename-Item $tempname_dir $newname -PassThru
            if (-Not $returned_object)
                {
                Write-Error "`nCannot rename " $tempname_dir " directory to " $newname
                Return 1
                }
        }
        Write-Host "`n" $from " copied to : " (Join-Path $to -ChildPath $newname)
        Return 0
    }

    Write-Error "`nCannot copy " $from " directory to " (Join-Path $to -ChildPath $newname)
    Return 1
}

function ZipImage
{
    Write-Host "`nCompressing IMG file"
    $new_zip_img = $zip_img.Replace(".zip", "_$vca_image_version.zip")
    $destination =  (Join-Path $output_dir -ChildPath $new_zip_img)

    If(Test-path $destination) {Remove-item $destination}

    Move-Item ($output_dir + '\*.img') $tmp_dir

    Add-Type -assembly "system.io.compression.filesystem"
    $compressionLevel= [System.IO.Compression.CompressionLevel]::Fastest
    $includeBaseDirectory = $false

    [io.compression.zipfile]::CreateFromDirectory($tmp_dir, $destination, $compressionLevel, $includeBaseDirectory)
    Write-Host "`nDeleting IMG file"

    del ($tmp_dir + '\*.img')

    if(-Not (Test-Path $destination)) { Write-Error "Compression failed"; Return 1 }
    Return 0
}


# Main flow


$status = 0
$rc=0

$StopWatch = [System.Diagnostics.Stopwatch]::StartNew()

# not need to check return code, function only print variable used in execution of script
SummarizeParameters

$rc = ((GenerateImage)[-1])
if ($rc -ne 0)
{
    Write-Host "`nGenerating vhd image failed"
    Exit $rc
}


$rc = CreateIsoFromVhd
if ($rc -ne 0)
{
    Exit $rc
}

$rc = ((ZipImage)[-1])
	if ($rc -ne 0)
	{
		Exit $rc
	}

$StopWatch.Elapsed

if ($status -eq 0)
{
    Write-Host "`nSuccessfully created ISO file with Windows over blockio"
}
else
{
    Write-Error "`nISO creation completed with error:"
    Switch ($status)
    {
        1 {Write-Error " install VCA drivers to VHD error`n"}
        2 {Write-Error " copy GFX driver error`n"}
        3 {Write-Error " copy VCAgent error`n"}
        4 {Write-Error " copy OpenSSH error`n"}
        5 {Write-Error " copy Xen error`n"}
    }
    Throw $status
}
$Stopwatch.Stop()
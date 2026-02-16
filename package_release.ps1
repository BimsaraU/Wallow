# package_release.ps1
# This script builds the project in Release mode and packages the executable into a zip file.

$ErrorActionPreference = "Stop"

Write-Host "Cleaning build directory..."
if (Test-Path "build") {
    Remove-Item -Path "build" -Recurse -Force
}

Write-Host "Configuring CMake..."
# We let CMake pick the default generator (likely Visual Studio)
cmake -B build -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
    exit 1
}

Write-Host "Building project..."
cmake --build build --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    exit 1
}

$releaseDir = "Wallow_Release"
if (Test-Path $releaseDir) {
    Remove-Item -Path $releaseDir -Recurse -Force
}
New-Item -ItemType Directory -Path $releaseDir | Out-Null

# Location depends on generator. VS uses Release subfolder, Ninja/Makefiles put it directly in build.
$exePath = "build/Release/Wallow.exe"
if (-not (Test-Path $exePath)) {
    $exePath = "build/Wallow.exe"
}

if (Test-Path $exePath) {
    Write-Host "Found executable at $exePath"
    Copy-Item -Path $exePath -Destination "$releaseDir/Wallow.exe"
    
    # Copy README.md as well
    Copy-Item -Path "README.md" -Destination "$releaseDir/README.md"
    
    $zipFile = "Wallow_Release.zip"
    if (Test-Path $zipFile) {
        Remove-Item -Path $zipFile -Force
    }
    
    Write-Host "Creating zip package..."
    Compress-Archive -Path "$releaseDir\*" -DestinationPath $zipFile
    
    Write-Host "Build and packaging complete! Created $zipFile"
} else {
    Write-Error "Could not find Wallow.exe in build output."
    exit 1
}

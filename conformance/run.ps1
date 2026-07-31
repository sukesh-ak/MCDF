# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# Score an MCDF implementation against the conformance vectors - the Windows
# twin of run.sh, deliberately kept in step with it (same checks, same output,
# same exit codes).
#
#   .\run.ps1                        # score `mcdf` from PATH (the reference runtime)
#   .\run.ps1 C:\path\to\my-cli      # score any CLI exposing the MCDF verbs
#   .\run.ps1 C:\path\to\my-cli dir  # also score the OPTIONAL directory form
#
# The CLI under test must support:
#   <cli> validate <container> --profile <core|integrity|signed|encrypted|render>
#                                                           exit 0 = valid
#   <cli> manifest <container>                              canonical JSON on stdout
#
# Every vector is published in both serializations: `container/` to read and
# diff, `container.mcdf` to hand over. Scoring defaults to the TAR form because
# spec section 3 makes it the one REQUIRED serialization - it is what any two
# implementations are guaranteed to share, and an implementation with no
# filesystem can be scored on nothing else. Pass `dir` to score the OPTIONAL
# directory form instead, which only implementations that support it will pass.
#
# An implementation may stop partway up the profile ladder. One that says so -
# by failing with E_UNIMPLEMENTED, which errors.md reserves for exactly this -
# is scored "not implemented" for that vector rather than passed or failed, and
# the count is printed. Anything else it reports is scored normally, so
# declining a profile buys nothing but honesty about which profiles it claims.
#
# Why this exists: run.sh is POSIX sh, so ctest skipped the kit on Windows
# entirely. That gap let a real defect hide - the CLI's stdout was in text mode,
# so `mcdf manifest` emitted CRLF and was not byte-exact on Windows, and no job
# ever noticed. A byte-exact format deserves to be checked on every platform it
# claims to support.
#
# Windows PowerShell 5.1 compatible: no ternary, no null-coalescing, no
# ProcessStartInfo.ArgumentList.

[CmdletBinding()]
param(
  [string]$Cli = 'mcdf',
  [ValidateSet('tar', 'dir')]
  [string]$Form = 'tar'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:passed = 0
$script:failed = 0
$script:skipped = 0

# The container path for a vector, in the selected serialization.
function Get-Container([string]$vectorDir) {
  if ($Form -eq 'tar') { return (Join-Path $vectorDir 'container.mcdf') }
  return (Join-Path $vectorDir 'container')
}

$script:repackHint = 'repack with cmake -DMODE=pack -P cmake/vectors.cmake'

function Resolve-Cli([string]$candidate) {
  if (Test-Path -LiteralPath $candidate -PathType Leaf) {
    return (Resolve-Path -LiteralPath $candidate).Path
  }
  $found = Get-Command $candidate -CommandType Application -ErrorAction SilentlyContinue
  if ($found) { return $found.Source }
  return $null
}

# Run the CLI and return its exit code, raw stdout bytes, and stderr text.
# .NET is used rather than PowerShell's own redirection because 5.1 rewrites
# newlines on `>` and wraps native stderr in ErrorRecords - either would defeat
# a byte-for-byte comparison.
function Invoke-Cli([string]$exe, [string[]]$cliArgs) {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $exe
  $psi.Arguments = ($cliArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true

  $proc = [System.Diagnostics.Process]::Start($psi)
  $buffer = New-Object System.IO.MemoryStream
  $proc.StandardOutput.BaseStream.CopyTo($buffer)
  $stderrText = $proc.StandardError.ReadToEnd()
  $proc.WaitForExit()

  $bytes = $buffer.ToArray()
  return [pscustomobject]@{
    Code   = $proc.ExitCode
    Bytes  = $bytes
    StdOut = [System.Text.Encoding]::UTF8.GetString($bytes)
    StdErr = $stderrText
  }
}

function Get-Field($json, [string]$name) {
  if ($json.PSObject.Properties.Name -contains $name) { return [string]$json.$name }
  return ''
}

function Add-Result([string]$name, [bool]$ok, [string]$detail) {
  if ($ok) {
    Write-Host ("  PASS  {0}" -f $name)
    $script:passed++
  } else {
    $suffix = ''
    if ($detail) { $suffix = " - $detail" }
    Write-Host ("  FAIL  {0}{1}" -f $name, $suffix)
    $script:failed++
  }
}

function Add-Skipped([string]$name, [string]$detail) {
  $suffix = ''
  if ($detail) { $suffix = " - $detail" }
  Write-Host ("  N/A   {0}{1}" -f $name, $suffix)
  $script:skipped++
}

# True when the run declined the profile rather than judging the document.
function Test-Declined($result) {
  if ($result.Code -eq 0) { return $false }
  return (($result.StdOut + $result.StdErr) -like '*E_UNIMPLEMENTED*')
}

$cliPath = Resolve-Cli $Cli
if (-not $cliPath) {
  [Console]::Error.WriteLine("error: CLI not found: $Cli")
  exit 2
}

Write-Host 'MCDF conformance kit'
Write-Host "implementation: $cliPath"
if ($Form -eq 'tar') {
  Write-Host 'serialization:  tar (REQUIRED, spec 3)'
} else {
  Write-Host 'serialization:  directory (OPTIONAL)'
}
Write-Host ''

Write-Host 'validate (valid + invalid vectors)'
$caseDirs = @()
foreach ($group in @('valid', 'invalid')) {
  $root = Join-Path $here (Join-Path 'vectors' $group)
  if (Test-Path -LiteralPath $root) {
    $caseDirs += Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name
  }
}

foreach ($dir in $caseDirs) {
  $casePath = Join-Path $dir.FullName 'case.json'
  if (-not (Test-Path -LiteralPath $casePath)) { continue }

  $case = Get-Content -LiteralPath $casePath -Raw | ConvertFrom-Json
  # `$profile` is an automatic variable in PowerShell - do not reuse the name.
  $profileName = Get-Field $case 'profile'
  if (-not $profileName) { $profileName = 'integrity' }
  $expect = Get-Field $case 'expect'
  $wantErr = Get-Field $case 'error'

  $container = Get-Container $dir.FullName
  if (-not (Test-Path -LiteralPath $container)) {
    Add-Result $dir.Name $false "missing $Form form - $script:repackHint"
    continue
  }
  $result = Invoke-Cli $cliPath @('validate', $container, '--profile', $profileName)
  if (Test-Declined $result) {
    Add-Skipped $dir.Name "$profileName not implemented"
    continue
  }
  $combined = $result.StdOut + $result.StdErr

  if ($expect -eq 'pass') {
    if ($result.Code -eq 0) {
      Add-Result $dir.Name $true ''
    } else {
      Add-Result $dir.Name $false ("expected pass, got: " + $combined.Trim())
    }
  } else {
    if ($result.Code -eq 0) {
      Add-Result $dir.Name $false "expected rejection ($wantErr)"
    } elseif ($wantErr -and ($combined -notlike "*$wantErr*")) {
      Add-Result $dir.Name $false "rejected, but not with $wantErr"
    } else {
      Add-Result $dir.Name $true ''
    }
  }
}

function Test-SameBytes($produced, [string]$expectedPath) {
  $want = [System.IO.File]::ReadAllBytes($expectedPath)
  if ($produced.Length -ne $want.Length) { return $false }
  for ($i = 0; $i -lt $want.Length; $i++) {
    if ($produced[$i] -ne $want[$i]) { return $false }
  }
  return $true
}

Write-Host ''
Write-Host 'canonical bytes (byte-for-byte)'
$canonicalRoot = Join-Path $here (Join-Path 'vectors' 'canonical')
if (Test-Path -LiteralPath $canonicalRoot) {
  foreach ($dir in (Get-ChildItem -LiteralPath $canonicalRoot -Directory | Sort-Object Name)) {
    $casePath = Join-Path $dir.FullName 'case.json'
    if (-not (Test-Path -LiteralPath $casePath)) { continue }

    $case = Get-Content -LiteralPath $casePath -Raw | ConvertFrom-Json
    $check = Get-Field $case 'check'
    if (-not $check) { $check = 'canonical-manifest' }
    $container = Get-Container $dir.FullName
    if (-not (Test-Path -LiteralPath $container)) {
      Add-Result $dir.Name $false "missing $Form form - $script:repackHint"
      continue
    }

    if ($check -eq 'canonical-manifest') {
      $expectedPath = Join-Path $dir.FullName (Join-Path 'expected' 'manifest.json')
      $result = Invoke-Cli $cliPath @('manifest', $container)
      if (Test-Declined $result) {
        Add-Skipped $dir.Name 'canonical manifest not implemented'
      } elseif (Test-SameBytes $result.Bytes $expectedPath) {
        Add-Result $dir.Name $true ''
      } else {
        Add-Result $dir.Name $false 'output differs from expected/manifest.json'
      }
    } elseif ($check -eq 'canonical-render') {
      # Both renderings are scored, and separately: an implementation that has
      # the HTML exactly right and the plain text wrong has one bug, not none.
      foreach ($fmt in @('html', 'text')) {
        if ($fmt -eq 'html') { $file = 'render.html' } else { $file = 'render.txt' }
        $expectedPath = Join-Path $dir.FullName (Join-Path 'expected' $file)
        $result = Invoke-Cli $cliPath @('render', $fmt, $container)
        if (Test-Declined $result) {
          Add-Skipped ("{0} ({1})" -f $dir.Name, $fmt) 'canonical render not implemented'
        } elseif (Test-SameBytes $result.Bytes $expectedPath) {
          Add-Result ("{0} ({1})" -f $dir.Name, $fmt) $true ''
        } else {
          Add-Result ("{0} ({1})" -f $dir.Name, $fmt) $false "output differs from $file"
        }
      }
    } else {
      Add-Result $dir.Name $false "unknown check: $check"
    }
  }
}

Write-Host ''
Write-Host '-------------------------------------'
if ($script:skipped -eq 0) {
  Write-Host ("passed {0}, failed {1}" -f $script:passed, $script:failed)
} else {
  # Printed, never hidden: a score of "0 failed" means something different when
  # a third of the vectors were never evaluated, and the reader of this line is
  # entitled to know which.
  Write-Host ("passed {0}, failed {1}, not implemented {2}" -f
              $script:passed, $script:failed, $script:skipped)
}
if ($script:failed -ne 0) { exit 1 }
exit 0

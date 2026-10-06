[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$PreviousVersion,
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$ArtifactDirectory = 'artifact',
    [string]$ServerUrl = 'https://github.com'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = (Get-WebmBuildVersion $Version).Name
if ($PreviousVersion -and -not (Read-WebmVersionTag $PreviousVersion)) { throw 'Invalid previous release version.' }
if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'Invalid repository or commit.'
}
$archiveName = "ttp_webm-$version.zip"
$hash = (Get-FileHash -LiteralPath (Join-Path $ArtifactDirectory $archiveName) -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = (Get-Content -LiteralPath (Join-Path $ArtifactDirectory 'SHA256SUMS.txt') -Encoding UTF8 -Raw).Trim()
if ($expected -cne "$hash  $archiveName") { throw 'WebM package SHA-256 verification failed.' }
$repoUrl = "$ServerUrl/$Repository"
$logUrl = if ($PreviousVersion) { "$repoUrl/compare/$PreviousVersion...$version" } else { "$repoUrl/commits/$version" }
$notes = @"
**更新记录**: $logUrl

关闭播放器后，将 ZIP 中的 AddIn/ttp_webm.dll 放入播放器安装目录。
支持 WebM/WebA 的 Opus、Vorbis 音轨播放、定位及标签修改、删除、保存。
支持 PNG/JPEG 附件封面添加、替换、删除；封面采用 Matroska Attachments 兼容扩展，不保证所有 WebM 工具都能显示。
保存先暂存校验并备份；原版最终释放 Reader 时提交，重建版可取得真实提交结果。
支持带视频的 WebM，仅播放音频；不提供音频编码功能。
同一 x86 DLL 兼容 XP SP3、Win7 和新系统；CPU 需要 SSE2。
ZIP 只包含 AddIn/ttp_webm.dll 和 SHA256SUMS.txt。

[源码、构建和验证说明]($repoUrl/tree/$Commit)
[第三方来源与许可证]($repoUrl/blob/$Commit/third_party/ORIGIN.md)
"@
$notes | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'release-notes.md') -Encoding UTF8

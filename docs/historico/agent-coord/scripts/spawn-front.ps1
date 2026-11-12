# Spawn isolated worktree for a front. Usage:
#   .\agent-coord\scripts\spawn-front.ps1 -Front de-load
param(
  [Parameter(Mandatory=$true)][string]$Front,
  [string]$Repo = "C:\Users\Lucas\source\repos\zeebo_libretro",
  [string]$Parent = "D:\zeebo-worktrees",
  [string]$FromBranch = "wip/integration-20261106"
)
$ErrorActionPreference = "Stop"
$br = "front/$Front"
$wt = Join-Path $Parent $Front
if (Test-Path $wt) { Write-Host "Worktree already exists: $wt"; git -C $Repo worktree list; exit 0 }
New-Item -ItemType Directory -Force -Path $Parent | Out-Null
Push-Location $Repo
try {
  git rev-parse --verify $FromBranch | Out-Null
  if (-not (git show-ref --verify --quiet "refs/heads/$br")) {
    git branch $br $FromBranch
  }
  git worktree add $wt $br
  Write-Host "Created $wt on $br"
  Write-Host "Next: copy agent-coord/AGENT_PROMPT.md and set FRONT_ID=$Front"
} finally { Pop-Location }

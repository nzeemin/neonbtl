@echo off
setlocal disabledelayedexpansion
sort trace.log > trace.sort
if exist trace.uniq del trace.uniq
set "prev="
for /f "delims=" %%F in (trace.sort) do (
  set "curr=%%F"
  setlocal enabledelayedexpansion
  if !prev! neq !curr! echo !curr! >> trace.uniq
  endlocal
  set "prev=%%F"
)
del trace.sort
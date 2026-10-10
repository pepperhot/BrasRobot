@echo off
REM ============================================================
REM  start_lab.bat : simple lanceur -> start_lab.ps1 (logique réelle).
REM  Double-clic : démarre FTP anonyme + opérateur avec les réglages
REM  courants de lab.env (régénérés automatiquement à chaque lancement).
REM ============================================================
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_lab.ps1"

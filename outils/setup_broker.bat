@echo off
REM Clic droit > Executer en tant qu'administrateur (ou double-clic + "Oui").
REM Configure le broker MQTT avec mot de passe, une seule fois (permanent).
powershell -NoProfile -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','%~dp0setup_broker.ps1'"

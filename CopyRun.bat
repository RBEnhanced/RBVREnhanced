@echo off
echo Force closing game
taskkill /f /im rbvr.exe

echo Copying to game dir
copy x64\Debug\RBVREnhanced.dll D:\Games\harmonix-rockband-vr\
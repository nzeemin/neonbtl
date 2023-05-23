@echo off
set ASTYLEEXE=c:\bin\astyle.exe
set ASTYLEOPT=-n -Q --options=..\astyle-cpp-options
%ASTYLEEXE% %ASTYLEOPT% emubase\*.h emubase\*.cpp
%ASTYLEEXE% %ASTYLEOPT% util\*.h --exclude=util\lz4.h
%ASTYLEEXE% %ASTYLEOPT% util\*.cpp --exclude=util\lz4.cpp
%ASTYLEEXE% %ASTYLEOPT% *.h --exclude=Version.h --exclude=stdafx.h
%ASTYLEEXE% %ASTYLEOPT% *.cpp --exclude=stdafx.cpp

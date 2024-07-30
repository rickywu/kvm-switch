# kvm-switch for Windows

Co-work with USB switch, detect any USB device plug-in, then change monitor input source if it support DDC.

After that detect wheather console is locked, then press UP arrow or ALT key to active password/PIN input field.

# Compile
You should use Visual Studio Build Toolkit
```
cl kvm.cpp
```

# Run
Run it in background, work like this
```
Config loaded: VID=046D, PID=C52B, inputSource=15
[2024-07-30 15:50:02] Display input source changed to: 15
Locked
```

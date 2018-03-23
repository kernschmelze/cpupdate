# cpupdate
Microcode utility for BSD

The <b>manual page</b> is not yet converted to nroff, read it here please:<br>
http://bsd.denkverbot.info/2018/03/cpupdate-manpage-suggestion.html<br>

Note the program and the installer are still WIP.<br>
Please do not install via pkg, only with ports.<br>

Do "cd /usr/ports/sysutils/cpupdate".<br>
Then run "make config".<br>
In the dialog shown, activate the options for downloading microcode files.<br>
Then do "make install".<br>
You get a notice printed where the microcode files that have been downloaded have been placed.<br>
Now you have to place them into where the program expects them.<br>

<b>To install the Intel microcode pack:</b>
As root, do "mkdir -p /usr/local/share/cpupdate/CPUMicrocodes/primary/Intel".<br>
Then do "mv /usr/ports/sysutils/cpupdate/work/intel-ucode/* /usr/local/share/cpupdate/CPUMicrocodes/primary/Intel"<br>
This moves the Intel microcode files to the directory cpupdate expects them in.<br>

<b>To install the platomav/CPUMicrocodes collection:</b><br>
As root, do "mkdir -p /usr/local/share/cpupdate/CPUMicrocodes/secondary/Intel".<br>
Then do "cpupdate -IC -S /usr/ports/sysutils/cpupdate/work/CPUMicrocodes-2ece631/Intel -T /usr/local/share/cpupdate/CPUMicrocodes/secondary/Intel".<br>
This converts the legacy-format microcode files to modern Intel multi-blobbed format ready-to-use by cpupdate.<br>

There are also some temporary notes, covering the directories used etc:<br>
http://bsd.denkverbot.info/2018/03/notes-for-making-sysutilscpupdate-port.html<br>

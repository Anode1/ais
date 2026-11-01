AIS -- your memory, yours to keep.   (Windows, installed)

Open it:      "AIS" in the Start menu, or the AIS icon on the Desktop. It opens
              in your browser and puts an icon in the notification area (the
              corner by the clock, behind the ^ if hidden): left-click it to
              open AIS again, right-click it to Quit.
Command line: open a terminal and run
                  ais --help            e.g.  ais venice italy
                  ais --where           print where your memory is stored
              (the installer added this folder to your PATH; a terminal opened
              before the install does not see it yet)

Your memory (the index) lives in your user profile, not in this folder:
    %USERPROFILE%\.ais
so removing AIS (Settings > Apps) never touches it. It is plain text you can
read, back up, or copy elsewhere.

ais.exe is the command line; opened by a double-click it only prints its help.
ais-gui.exe is the window: it runs ais.exe as a local server and opens the page.

No .NET, no runtime: both programs are self-contained.
New here? Open USING.txt for a one-minute guide, or about.txt for what AIS is.

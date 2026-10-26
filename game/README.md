Put the game disc image here: `Armored Core - Nine Breaker (USA).iso` (the USA release, SLUS-21200).

Stage 0 looks for that file name first, then takes the first `*.iso` in this folder. The image's SHA-1 is checked
at launch, so any file name works as long as it is the right disc. A hard link to an ISO elsewhere on the same drive
(`mklink /H` / `New-Item -ItemType HardLink`) and a plain copy behave the same. The image is never versioned
(.gitignore). `disc.path` in port/build/acnb.ini or the ACNB_ISO environment variable override this folder.

# RBVREnhanced

A work-in-progress quality of life mod for Rock Band VR. More details to come!

If you run into any issues, check the [issue tracker](https://github.com/RBEnhanced/RBVREnhanced/issues).

## Features

* ARKless/Raw file loading for mod support (such as available at https://github.com/LlysiX/RBVRE-Patches)
* Removes Oculus controller requirement
* Automatic detection of custom songs, installable via [LibForge](https://github.com/mtolly/LibForge)
    * TODO: split songs into "songs" subdirectory.

## Usage/Installation

1. Download the latest build from [AppVeyor](https://ci.appveyor.com/project/InvoxiPlayGames/rbvrenhanced/build/artifacts).
2. Extract the zip file into the directory your RBVR installation is located.
    * Usually, this is at `C:\Program Files\Oculus\Software\Software\harmonix-rockband-vr`, but this may differ depending on how you installed the game.
3. Launch Rock Band VR, through either the Oculus software or by opening rbvr.exe, depending on VR setup.

## Building

Visual Studio 2022 with the Visual Studio 2017 (v141) build tools installed should work, NuGet must be working to download minhook.

TODO: proper build guide, GitHub actions

## License

RBVREnhanced is licensed under the GNU General Public License version 2, or any later version at your choice.

* inih / INIReader: https://github.com/benhoyt/inih
    * Used under the 3-clause BSD license.
* minhook: https://github.com/TsudaKageyu/minhook
    * Used under the 2-clause BSD license.

<div align="center">

# PS3-XMB Desktop Environment

[![C](https://img.shields.io/badge/Language-C-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C_(programming_language))
[![C++](https://img.shields.io/badge/Language-C++-%2300599C.svg?style=for-the-badge&logo=c%2B%2B&logoColor=white)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Make](https://img.shields.io/badge/Build-Makefile-%234CC3D9.svg?style=for-the-badge&logo=gnu&logoColor=white)](https://www.gnu.org/software/make/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%2F%20Unix-informational?style=for-the-badge&logo=linux&logoColor=white)](https://www.kernel.org/)

A lightweight desktop environment inspired by the iconic PlayStation 3 XrossMediaBar (XMB).

</div>

---

##   Installation & Setup

<steps>

<step title="Prepare the Icons" subtitle="Step 1">
Decompress the <code>Icons.zip</code> archive. 

Compatible whit:x86_64,(coming soon arm64)

Make sure the resulting <code>Icons</code> folder is placed directly in the **root directory** alongside your core files:
* <code>background</code>
* <code>sound</code>
* <code>makefile</code>
* <code>menu.xml</code>
</step>

<step title="Build, Test, and Install" subtitle="Step 2">
Open your terminal in the root directory and run the clean build commands:

```bash
make clean && make

After building, you can test the desktop environment by doing:

make run

Or, install the desktop environment to your PC:

make install

(To uninstall later, simply do make uninstall)

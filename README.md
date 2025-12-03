# Direct3D8 Hook & Custom Rendering Project

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Visual Studio 2015](https://img.shields.io/badge/IDE-Visual%20Studio%202015-5C2D91.svg)](https://visualstudio.microsoft.com/)
[![Platform](https://img.shields.io/badge/Platform-Windows-0078d7.svg)](https://en.wikipedia.org/wiki/Microsoft_Windows)

> **Note**: This project is compiled with Visual Studio 2015. The `d3d8_h` component is prepared for SDK-less compilation and can be removed as appropriate.

## 🧠 Core Concept / 核心思路

The core idea of this project is to hijack `d3d8.dll`, hook the `CreateDevice` function to obtain the device pointer. Other exported functions are forwarded sequentially to the original system DLL.
项目的核心思路是劫持 `d3d8.dll`，通过钩取（Hook）`CreateDevice` 函数来获取设备指针，其他导出函数按序直接跳转到原始的系统DLL。

## ⚙️ How It Works / 实现方式

1.  **DLL Hijacking (DLL 劫持)**: A proxy `d3d8.dll` is placed in the target application's directory, causing it to load our DLL instead of the system one.
    将一个代理 `d3d8.dll` 置于目标应用程序目录，使其优先加载我们的DLL而非系统DLL。

2.  **Function Forwarding (函数转发)**: All exported functions from the original `d3d8.dll` are forwarded, except for the targeted ones. This ensures basic compatibility.
    除目标函数外，原始 `d3d8.dll` 的所有导出函数均被转发，以确保基本兼容性。

3.  **VTable Hooking (VTable 钩子)**: The `CreateDevice` function is hooked to intercept the call and obtain the `IDirect3DDevice8` (or similar) interface pointer. Subsequently, VTable hooking is employed on the acquired device to intercept specific rendering methods (e.g., `Present`, `DrawIndexedPrimitive`).
    钩取 `CreateDevice` 函数以拦截调用并获取设备接口指针。随后，对获取到的设备使用VTable钩子技术，以拦截特定的渲染方法。

4.  **Custom Rendering (自定义渲染)**: Once the necessary methods are hooked, custom rendering logic (e.g., drawing images, overlays) can be injected into the application's render loop.
    一旦必要的函数被钩取，即可在应用程序的渲染循环中注入自定义的渲染逻辑（例如绘制图片、叠加层）。

## 🚀 Build / 编译指南

1.  **Prerequisites / 环境要求**:
    *   Visual Studio 2015.
    *   Appropriate Windows SDK.

2.  **Steps / 步骤**:
    *   Open the solution file (`[您的解决方案文件.sln]`) in Visual Studio 2015.
        在VS2015中打开解决方案文件。
    *   Ensure the configuration (e.g., Release, x86) matches your target.
        确保配置（如Release, x86）与你的目标匹配。
    *   Build the solution.
        编译解决方案。

## 📦 Usage / 使用方法

1.  After successful compilation, the output `d3d8.dll` will be generated.
    成功编译后，将生成输出的 `d3d8.dll` 文件。
2.  Place this `d3d8.dll` into the directory of the target application that uses Direct3D8.
    将此 `d3d8.dll` 放入使用Direct3D8的目标应用程序目录中。
3.  Launch the application. The custom rendering should take effect.
    启动应用程序。自定义渲染功能应生效。

## ⚠️ Disclaimer & Note / 免责声明与说明

*   This project is intended for educational and research purposes only. The use of code for any other purposes is the sole responsibility of the user.
    本项目仅用于教育和研究目的。将代码用于任何其他用途由用户自行负责。
*   The stability and compatibility of the hook may vary depending on the target application. Testing in different environments is recommended.
    钩子的稳定性和兼容性可能因目标应用程序而异，建议在不同环境中进行测试。
*   If you find any shortcomings or have suggestions for improvement, please feel free to point them out. Thank you!
    如果您发现任何不足或有改进建议，请指出，谢谢！

## 🙏 Acknowledgments / 致谢

*   Thanks to the developers and researchers who shared knowledge related to DirectX hooking.
    感谢分享DirectX钩子相关知识的开发者和研究人员。

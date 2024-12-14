#include "PlatformUtils.hpp"

#include "vulkan_template/core/Log.hpp"
#include <GLFW/glfw3.h> // IWYU pragma: keep
#include <ShObjIdl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <combaseapi.h>
#include <filesystem>
#include <minwindef.h>
#include <objbase.h>
#include <optional>
#include <vector>
#include <windef.h>
#include <winerror.h>
#include <winscard.h>
#include <wtypesbase.h>

namespace
{
auto convertCharToWChar(std::string const& string)
    -> std::optional<std::wstring>
{
    int32_t constexpr BUFFER_LENGTH{4096};

    std::optional<std::wstring> result{};

    wchar_t* buffer = new wchar_t[BUFFER_LENGTH];
    int32_t const charsWritten{MultiByteToWideChar(
        CP_ACP, 0, string.c_str(), -1, buffer, BUFFER_LENGTH
    )};
    if (charsWritten > 0)
    {
        result = std::wstring{buffer, static_cast<size_t>(charsWritten)};
    }
    delete[] buffer;
    return result;
}

auto getPathsFromDialog(
    LPCWSTR const title, HWND const parent, DWORD const additionalOptions
) -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> paths{};
    IFileOpenDialog* fileDialog;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&fileDialog)
        )))
    {
        return paths;
    }

    DWORD dwOptions;
    if (SUCCEEDED(fileDialog->GetOptions(&dwOptions)))
    {
        fileDialog->SetOptions(dwOptions | additionalOptions);
        fileDialog->SetTitle(title);
    }

    if (SUCCEEDED(fileDialog->Show(parent)))
    {
        IShellItemArray* items;
        if (SUCCEEDED(fileDialog->GetResults(&items)))
        {
            IEnumShellItems* itemsEnum;
            if (SUCCEEDED(items->EnumItems(&itemsEnum)))
            {
                do
                {
                    IShellItem* child;
                    ULONG fetched;
                    itemsEnum->Next(1, &child, &fetched);
                    if (fetched == FALSE)
                    {
                        break;
                    }

                    WCHAR* path;
                    child->GetDisplayName(SIGDN_FILESYSPATH, &path);
                    paths.emplace_back(path);
                    CoTaskMemFree(path);
                } while (TRUE);
                itemsEnum->Release();
            }
            items->Release();
        }
    }
    fileDialog->Release();

    return paths;
}
auto openDialog(
    std::string const& title,
    vkt::PlatformWindow const& parent,
    bool const pickFolders,
    bool const multiselect
) -> std::vector<std::filesystem::path>
{
    HRESULT const initResult{CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
    )};

    std::vector<std::filesystem::path> paths{};
    if (SUCCEEDED(initResult))
    {
        DWORD additionalOptions{FOS_NOCHANGEDIR};
        if (pickFolders)
        {
            additionalOptions |= FOS_PICKFOLDERS;
        }
        if (multiselect)
        {
            additionalOptions |= FOS_ALLOWMULTISELECT;
        }
        std::wstring const titleConverted{
            convertCharToWChar(title).value_or(std::wstring{L"Open"})
        };
        paths = getPathsFromDialog(
            titleConverted.c_str(),
            glfwGetWin32Window(parent.handle()),
            additionalOptions
        );
    }

    CoUninitialize();

    return paths;
}
} // namespace

namespace vkt
{
auto openFile(std::string const& title, PlatformWindow const& parent)
    -> std::optional<std::filesystem::path>
{
    std::vector<std::filesystem::path> paths{
        openDialog(title, parent, false, false)
    };

    if (paths.empty())
    {
        return std::nullopt;
    }

    if (paths.size() > 1)
    {
        VKT_WARNING("Dialog box returned more than 1 path, ignoring the rest.");
    }

    return paths[0];
}

auto openFiles(std::string const& title, PlatformWindow const& parent)
    -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> paths{
        openDialog(title, parent, false, true)
    };

    return paths;
}

auto openDirectory(std::string const& title, PlatformWindow const& parent)
    -> std::optional<std::filesystem::path>
{
    std::vector<std::filesystem::path> paths{
        openDialog(title, parent, true, false)
    };

    if (paths.empty())
    {
        return std::nullopt;
    }

    if (paths.size() > 1)
    {
        VKT_WARNING("Dialog box returned more than 1 path, ignoring the rest.");
    }

    return paths[0];
}

auto openDirectories(std::string const& title, PlatformWindow const& parent)
    -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> paths{
        openDialog(title, parent, true, true)
    };

    return paths;
}
} // namespace vkt
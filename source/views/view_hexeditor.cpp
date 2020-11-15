#include "views/view_hexeditor.hpp"

#include "providers/provider.hpp"
#include "providers/file_provider.hpp"

#include <GLFW/glfw3.h>

namespace hex {

    ViewHexEditor::ViewHexEditor(prv::Provider* &dataProvider, std::vector<hex::PatternData*> &patternData)
            : View(), m_dataProvider(dataProvider), m_patternData(patternData) {

        this->m_memoryEditor.ReadFn = [](const ImU8 *data, size_t off) -> ImU8 {
            ViewHexEditor *_this = (ViewHexEditor *) data;

            if (!_this->m_dataProvider->isAvailable() || !_this->m_dataProvider->isReadable())
                return 0x00;

            ImU8 byte;
            _this->m_dataProvider->read(off, &byte, sizeof(ImU8));

            return byte;
        };

        this->m_memoryEditor.WriteFn = [](ImU8 *data, size_t off, ImU8 d) -> void {
            ViewHexEditor *_this = (ViewHexEditor *) data;

            if (!_this->m_dataProvider->isAvailable() || !_this->m_dataProvider->isWritable())
                return;

            _this->m_dataProvider->write(off, &d, sizeof(ImU8));
            _this->postEvent(Events::DataChanged);
        };

        this->m_memoryEditor.HighlightFn = [](const ImU8 *data, size_t off, bool next) -> bool {
            ViewHexEditor *_this = (ViewHexEditor *) data;

            for (auto& pattern : _this->m_patternData) {
                if (next && off == (pattern->getOffset() + pattern->getSize())) {
                    return false;
                }

                if (off >= pattern->getOffset() && off < (pattern->getOffset() + pattern->getSize())) {
                    _this->m_memoryEditor.HighlightColor = pattern->getColor();
                    return true;
                }
            }

            _this->m_memoryEditor.HighlightColor = 0x50C08080;
            return false;
        };
    }

    ViewHexEditor::~ViewHexEditor() {
        if (this->m_dataProvider != nullptr)
            delete this->m_dataProvider;
        this->m_dataProvider = nullptr;
    }

    void ViewHexEditor::createView() {
        if (!this->m_memoryEditor.Open)
            return;

        size_t dataSize = (this->m_dataProvider == nullptr || !this->m_dataProvider->isReadable()) ? 0x00 : this->m_dataProvider->getSize();

        this->m_memoryEditor.DrawWindow("Hex Editor", this, dataSize);

        if (dataSize != 0x00) {
            this->drawSearchPopup();
            this->drawGotoPopup();
        }

        this->m_fileBrowser.Display();

        if (this->m_fileBrowser.HasSelected()) {
            if (this->m_dataProvider != nullptr)
                delete this->m_dataProvider;

            this->m_dataProvider = new prv::FileProvider(this->m_fileBrowser.GetSelected().string());
            View::postEvent(Events::DataChanged);
            this->m_fileBrowser.ClearSelected();
        }

    }

    void ViewHexEditor::copyBytes() {
        size_t copySize = (this->m_memoryEditor.DataPreviewAddrEnd - this->m_memoryEditor.DataPreviewAddr) + 1;

        std::vector<u8> buffer(copySize, 0x00);
        this->m_dataProvider->read(this->m_memoryEditor.DataPreviewAddr, buffer.data(), buffer.size());

        std::string str;
        for (const auto &byte : buffer)
            str += hex::format("%x ", byte);
        str.pop_back();

        ImGui::SetClipboardText(str.c_str());
    }

    void ViewHexEditor::copyString() {
        size_t copySize = (this->m_memoryEditor.DataPreviewAddrEnd - this->m_memoryEditor.DataPreviewAddr) + 1;

        std::string buffer;
        buffer.reserve(copySize + 1);
        this->m_dataProvider->read(this->m_memoryEditor.DataPreviewAddr, buffer.data(), copySize);

        ImGui::SetClipboardText(buffer.c_str());
    }

    void ViewHexEditor::createMenu() {

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File...", "CTRL + O")) {
                this->m_fileBrowser.SetTitle("Open File");
                this->m_fileBrowser.Open();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Search", "CTRL + F")) {
                View::doLater([]{ ImGui::OpenPopup("Search"); });
            }

            if (ImGui::MenuItem("Goto", "CTRL + G")) {
                View::doLater([]{ ImGui::OpenPopup("Goto"); });
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Copy bytes", "CTRL + ALT + C")) {
                this->copyBytes();
            }

            if (ImGui::MenuItem("Copy string", "CTRL + SHIFT + C")) {
                this->copyString();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hex View", "", &this->m_memoryEditor.Open);
            ImGui::EndMenu();
        }
    }

    bool ViewHexEditor::handleShortcut(int key, int mods) {
        if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_F) {
            ImGui::OpenPopup("Search");
            return true;
        } else if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_G) {
            ImGui::OpenPopup("Goto");
            return true;
        } else if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_O) {
            this->m_fileBrowser.SetTitle("Open File");
            this->m_fileBrowser.Open();
            return true;
        } else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_ALT) && key == GLFW_KEY_C) {
            this->copyBytes();
            return true;
        } else if (mods == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT) && key == GLFW_KEY_C) {
            this->copyString();
            return true;
        }

        return false;
    }


    static std::vector<std::pair<u64, u64>> findString(prv::Provider* &provider, std::string string) {
        std::vector<std::pair<u64, u64>> results;

        u32 foundCharacters = 0;

        std::vector<u8> buffer(1024, 0x00);
        size_t dataSize = provider->getSize();
        for (u64 offset = 0; offset < dataSize; offset += 1024) {
            size_t usedBufferSize = std::min(buffer.size(), dataSize - offset);
            provider->read(offset, buffer.data(), usedBufferSize);

            for (u64 i = 0; i < usedBufferSize; i++) {
                if (buffer[i] == string[foundCharacters])
                    foundCharacters++;
                else
                    foundCharacters = 0;

                if (foundCharacters == string.size()) {
                    results.emplace_back(offset + i - foundCharacters + 1, offset + i + 1);
                    foundCharacters = 0;
                }
            }
        }

        return results;
    }

    static std::vector<std::pair<u64, u64>> findHex(prv::Provider* &provider, std::string string) {
        std::vector<std::pair<u64, u64>> results;

        if ((string.size() % 2) == 1)
            string = "0" + string;

        std::vector<u8> hex;
        hex.reserve(string.size() / 2);

        for (u32 i = 0; i < string.size(); i += 2) {
            char byte[3] = { string[i], string[i + 1], 0 };
            hex.push_back(strtoul(byte, nullptr, 16));
        }

        u32 foundCharacters = 0;

        std::vector<u8> buffer(1024, 0x00);
        size_t dataSize = provider->getSize();
        for (u64 offset = 0; offset < dataSize; offset += 1024) {
            size_t usedBufferSize = std::min(buffer.size(), dataSize - offset);
            provider->read(offset, buffer.data(), usedBufferSize);

            for (u64 i = 0; i < usedBufferSize; i++) {
                if (buffer[i] == hex[foundCharacters])
                    foundCharacters++;
                else
                    foundCharacters = 0;

                if (foundCharacters == hex.size()) {
                    results.emplace_back(offset + i - foundCharacters + 1, offset + i + 1);
                    foundCharacters = 0;
                }
            }
        }

        return results;
    }


    void ViewHexEditor::drawSearchPopup() {
        static auto InputCallback = [](ImGuiInputTextCallbackData* data) -> int {
            auto _this = static_cast<ViewHexEditor*>(data->UserData);

            *_this->m_lastSearchBuffer = _this->m_searchFunction(_this->m_dataProvider, data->Buf);
            _this->m_lastSearchIndex = 0;

            if (_this->m_lastSearchBuffer->size() > 0)
                _this->m_memoryEditor.GotoAddrAndHighlight((*_this->m_lastSearchBuffer)[0].first, (*_this->m_lastSearchBuffer)[0].second);

            return 0;
        };

        static auto Find = [this](char *buffer) {
            *this->m_lastSearchBuffer = this->m_searchFunction(this->m_dataProvider, buffer);
            this->m_lastSearchIndex = 0;

            if (this->m_lastSearchBuffer->size() > 0)
                this->m_memoryEditor.GotoAddrAndHighlight((*this->m_lastSearchBuffer)[0].first, (*this->m_lastSearchBuffer)[0].second);
        };

        static auto FindNext = [this]() {
            if (this->m_lastSearchBuffer->size() > 0) {
                ++this->m_lastSearchIndex %= this->m_lastSearchBuffer->size();
                this->m_memoryEditor.GotoAddrAndHighlight((*this->m_lastSearchBuffer)[this->m_lastSearchIndex].first,
                                                          (*this->m_lastSearchBuffer)[this->m_lastSearchIndex].second);
            }
        };

        static auto FindPrevious = [this]() {
            if (this->m_lastSearchBuffer->size() > 0) {
                this->m_lastSearchIndex--;

                if (this->m_lastSearchIndex < 0)
                    this->m_lastSearchIndex = this->m_lastSearchBuffer->size() - 1;

                this->m_lastSearchIndex %= this->m_lastSearchBuffer->size();

                this->m_memoryEditor.GotoAddrAndHighlight((*this->m_lastSearchBuffer)[this->m_lastSearchIndex].first,
                                                          (*this->m_lastSearchBuffer)[this->m_lastSearchIndex].second);
            }
        };

        if (ImGui::BeginPopup("Search")) {
            ImGui::TextUnformatted("Search");
            if (ImGui::BeginTabBar("searchTabs")) {
                char *currBuffer;
                if (ImGui::BeginTabItem("String")) {
                    this->m_searchFunction = findString;
                    this->m_lastSearchBuffer = &this->m_lastStringSearch;
                    currBuffer = this->m_searchStringBuffer;

                    ImGui::InputText("##nolabel", currBuffer, 0xFFFF, ImGuiInputTextFlags_CallbackCompletion,
                                     InputCallback, this);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Hex")) {
                    this->m_searchFunction = findHex;
                    this->m_lastSearchBuffer = &this->m_lastHexSearch;
                    currBuffer = this->m_searchHexBuffer;

                    ImGui::InputText("##nolabel", currBuffer, 0xFFFF,
                                     ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CallbackCompletion,
                                     InputCallback, this);
                    ImGui::EndTabItem();
                }

                if (ImGui::Button("Find"))
                    Find(currBuffer);

                if (this->m_lastSearchBuffer->size() > 0) {
                    if ((ImGui::Button("Find Next")))
                        FindNext();

                    ImGui::SameLine();

                    if ((ImGui::Button("Find Prev")))
                        FindPrevious();
                }

                ImGui::EndTabBar();
            }


            ImGui::EndPopup();
        }
    }

    void ViewHexEditor::drawGotoPopup() {
        if (ImGui::BeginPopup("Goto")) {
            ImGui::TextUnformatted("Goto");
            ImGui::InputScalar("##nolabel", ImGuiDataType_U64, &this->m_gotoAddress, nullptr, nullptr, "%llx", ImGuiInputTextFlags_CharsHexadecimal);

            if (this->m_gotoAddress >= this->m_dataProvider->getSize())
                this->m_gotoAddress = this->m_dataProvider->getSize() - 1;

            if (ImGui::Button("Goto")) {
                this->m_memoryEditor.GotoAddr = this->m_gotoAddress;
            }

            ImGui::EndPopup();
        }
    }

}
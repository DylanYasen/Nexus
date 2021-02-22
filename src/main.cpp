#define SDL_MAIN_HANDLED

#include <SDL.h>
#include <glad/glad.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <stdio.h>

#include "stb_image.h"
#include "cute_files.h"

#include "miniaudio.h"

const int WIDTH = 1024;
const int HEIGHT = 768;

enum Mode
{
	Texture,
	Audio
};

Mode activeMode = Mode::Texture;

struct TexturePreview
{
	int width;
	int height;
	GLuint textureId;
};

// todo: clean up
namespace AudioCallback
{
	void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
	{
		ma_decoder* pDecoder = (ma_decoder*)pDevice->pUserData;
		if (pDecoder == NULL) {
			return;
		}

		ma_decoder_read_pcm_frames(pDecoder, pOutput, frameCount);

		(void)pInput;
	}
}

struct AudioPreview
{
	ma_device_config deviceConfig;
	ma_device device;
	ma_decoder decoder;
	ma_result decodeResult;
	ma_result initResult;

	AudioPreview() {}

	AudioPreview(const char* filepath)
	{
		// todo: use format specific decoing api
		decodeResult = ma_decoder_init_file(filepath, NULL, &decoder);
		if (decodeResult != MA_SUCCESS)
		{
			printf("Failed to decode [%s].\n", filepath);
			return;
		}

		deviceConfig = ma_device_config_init(ma_device_type_playback);
		deviceConfig.playback.format = decoder.outputFormat;
		deviceConfig.playback.channels = decoder.outputChannels;
		deviceConfig.sampleRate = decoder.outputSampleRate;
		deviceConfig.dataCallback = AudioCallback::data_callback;
		deviceConfig.pUserData = &decoder;

		initResult = ma_device_init(NULL, &deviceConfig, &device);
		if (initResult != MA_SUCCESS) {
			printf("Failed to open playback device.\n");
		}
	}

	bool isValid() const {
		return
			decodeResult == MA_SUCCESS
			&&
			initResult == MA_SUCCESS;
	}

	bool Play()
	{
		if (!isValid())
		{
			printf("Audio not initialized\n");
			return false;
		}
		ma_result playResult = ma_device_start(&device);
		if (playResult != MA_SUCCESS) {
			printf("Failed to start playback device.\n");
			return false;
		}

		return true;
	}

	bool Stop()
	{
		if (ma_device_is_started(&device))
		{
			ma_decoder_seek_to_pcm_frame(&decoder, 0);
			ma_result stopResult = ma_device_stop(&device);
			return stopResult == MA_SUCCESS;
		}

		return false;
	}

	void Pause()
	{
		if (ma_device_is_started(&device))
		{
			ma_result stopResult = ma_device_stop(&device);

			if (stopResult != MA_SUCCESS) {
				printf("Failed to pause playback device.\n");
				Dispose();
			}
		}
	}

	void Dispose()
	{
		ma_device_uninit(&device);
		ma_decoder_uninit(&decoder);
	}

	~AudioPreview()
	{
		Dispose();
	}
};

bool LoadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height)
{
	// Load from file
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create a OpenGL texture identifier
	GLuint image_texture;
	glGenTextures(1, &image_texture);
	glBindTexture(GL_TEXTURE_2D, image_texture);

	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

	// Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	stbi_image_free(image_data);

	*out_texture = image_texture;
	*out_width = image_width;
	*out_height = image_height;

	return true;
}

int main(int argc, char const* argv[])
{
	SDL_Window* window = NULL;
	SDL_Surface* screenSurface = NULL;

	std::vector<cf_file_t> textureFiles;
	std::vector<cf_file_t> audioFiles;
	std::unordered_map<int, TexturePreview> texturePreviewMap;
	std::unordered_map<int, AudioPreview*> audioPreviewMap;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
	}
	else
	{
		// create window
		window = SDL_CreateWindow("Nexus",
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			WIDTH,
			HEIGHT,
			SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL /*| SDL_WINDOW_VULKAN*/);

		if (window == NULL)
		{
			printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
			return 1;
		}

		// create context

#if __APPLE__
// GL 3.2 Core + GLSL 150
		const char* glsl_version = "#version 150";
		SDL_GL_SetAttribute(
			SDL_GL_CONTEXT_FLAGS,
			SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
		// GL 3.0 + GLSL 130
		const char* glsl_version = "#version 130";
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
		SDL_GLContext context = SDL_GL_CreateContext(window);

		if (context == NULL)
		{
			printf("OpenGL context could not be created! SDL Error: %s\n",
				SDL_GetError());
			return 1;
		}

		if (!gladLoadGL())
		{
			printf("gladLoadGL failed");
			return 1;
		}

		// init imgui
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ImGui_ImplSDL2_InitForOpenGL(window, context);
		ImGui_ImplOpenGL3_Init(glsl_version);
		ImGuiIO& io = ImGui::GetIO(); (void)io;

		io.ConfigWindowsMoveFromTitleBarOnly = true;

		bool bRunning = true;
		bool bActive = true;

		const char* assetPaths[] = {
			"E:/Assets/actionrpgloot",
			"E:/Audio/RPG Sound Pack"
		};

		// load files
		{
			for (const auto& assetPath : assetPaths)
			{
				cf_dir_t dir;
				cf_dir_open(&dir, assetPath);

				while (dir.has_next)
				{
					cf_file_t file;
					cf_read_file(&dir, &file);
					//printf("%s\n", file.path);

					if (strcmp(file.ext, ".png") == 0 ||
						strcmp(file.ext, ".jpg") == 0)
					{
						textureFiles.push_back(file);
					}
					else if (
						//strcmp(file.ext, ".ogg") == 0 ||
						strcmp(file.ext, ".mp3") == 0 ||
						strcmp(file.ext, ".wav") == 0)
					{
						audioFiles.push_back(file);
					}

					cf_dir_next(&dir);
				}
				cf_dir_close(&dir);
			}
		}

		SDL_Event sdlEvent;
		while (bRunning)
		{
			while (SDL_PollEvent(&sdlEvent) != 0)
			{
				if (sdlEvent.type == SDL_QUIT)
				{
					bRunning = false;
				}
			}

			// imgui begin
			{
				ImGui_ImplOpenGL3_NewFrame();
				ImGui_ImplSDL2_NewFrame(window);
				ImGui::NewFrame();
			}

			ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Browser", &bActive, ImGuiWindowFlags_MenuBar))
			{
				if (ImGui::BeginMenuBar())
				{
					if (ImGui::BeginMenu("File"))
					{
						if (ImGui::MenuItem("Close")) bActive = false;
						ImGui::EndMenu();
					}
					ImGui::EndMenuBar();
				}

				// Left
				static int selected = 0;
				{
					ImGui::BeginChild("left pane", ImVec2(200, 0), true);

					if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None))
					{
						if (ImGui::BeginTabItem("Texture"))
						{
							for (int i = 0; i < textureFiles.size(); i++)
							{
								const auto& file = textureFiles[i];
								if (ImGui::Selectable(file.name, selected == i))
									selected = i;
							}
							ImGui::EndTabItem();

							activeMode = Mode::Texture;
						}

						if (ImGui::BeginTabItem("Audio"))
						{
							for (int i = 0; i < audioFiles.size(); i++)
							{
								const auto& file = audioFiles[i];
								if (ImGui::Selectable(file.name, selected == i))
									selected = i;
							}
							ImGui::EndTabItem();

							activeMode = Mode::Audio;
						}
					}
					ImGui::EndTabBar();

					ImGui::EndChild();
				}
				ImGui::SameLine();

				// Right
				{
					ImGui::BeginGroup();
					ImGui::BeginChild("item view", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())); // Leave room for 1 line below us

					if (activeMode == Mode::Texture)
					{
						const auto& file = textureFiles[selected];
						ImGui::Text("Name: %s", file.name);
						ImGui::Text("Format: %s", file.ext);
						ImGui::Text("Size: %d kb", file.size);

						if (texturePreviewMap.find(selected) == texturePreviewMap.end())
						{
							TexturePreview preview{};
							bool ret = LoadTextureFromFile(file.path,
								&preview.textureId,
								&preview.width, &preview.height);
							IM_ASSERT(ret);
							texturePreviewMap[selected] = preview;
						}

						ImGui::Separator();
						if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None))
						{
							if (ImGui::BeginTabItem("Description"))
							{
								const auto& preview = texturePreviewMap[selected];
								const float aspectRatio = (float)preview.width / (float)preview.height;
								if (preview.width > preview.height)
								{
									//w / h = 300 / x;
									float width = 300;
									float height = 300 / aspectRatio;

									ImGui::Image((void*)(intptr_t)preview.textureId,
										//ImVec2(preview.width, preview.height)
										ImVec2(width, height)
									);
								}
								else
								{
									//w / h = x / 300;
									float width = 300 * aspectRatio;
									float height = 300;

									ImGui::Image((void*)(intptr_t)preview.textureId,
										//ImVec2(preview.width, preview.height)
										ImVec2(width, height)
									);
								}

								ImGui::EndTabItem();
							}
							if (ImGui::BeginTabItem("Details"))
							{
								ImGui::Text("ID: 0123456789");
								ImGui::EndTabItem();
							}
							ImGui::EndTabBar();
						}
						ImGui::EndChild();
					}
					else if (activeMode == Mode::Audio)
					{
						const auto& file = audioFiles[selected];
						ImGui::Text("Name: %s", file.name);
						ImGui::Text("Format: %s", file.ext);
						ImGui::Text("Size: %d kb", file.size);

						if (audioPreviewMap.find(selected) == audioPreviewMap.end())
						{
							audioPreviewMap[selected] = new AudioPreview(file.path);
						}

						auto& const audioPreview = audioPreviewMap[selected];

						ImGui::Text("Sample Rate: %d Hz", audioPreview->decoder.outputSampleRate);
						ImGui::Text("Channel Count: %d", audioPreview->decoder.outputChannels);
						ImGui::EndChild();

						if (ImGui::Button("Play")) {
							audioPreview->Play();
						}
						ImGui::SameLine();

						if (ImGui::Button("Pause")) {
							audioPreview->Pause();
						}

						ImGui::SameLine();
						if (ImGui::Button("Stop")) {
							audioPreview->Stop();
						}
					}

					ImGui::EndGroup();
				}
			}
			ImGui::End();

			// imgui end
			{
				ImGui::Render();
				ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			}

			SDL_GL_SwapWindow(window);

			// clear
			{
				glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
				glClearColor(0, 0, 0, 0);
				glClear(GL_COLOR_BUFFER_BIT);
			}
		}

		// imgui clean up
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();

		return 0;
	}
}
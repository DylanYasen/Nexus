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

#include "IconsFontAwesome5.h"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fuzzy_match.h"

#include "sqlite_orm/sqlite_orm.h"

const int WIDTH = 1024;
const int HEIGHT = 768;

enum PreviewMode
{
	Texture,
	Audio
};



struct TexturePreview
{
	int width;
	int height;
	GLuint textureId;
};

namespace db {

	static const char* AUDIO_FILE_TYPE = "audio";
	static const char* TEXTURE_FILE_TYPE = "texture";

	struct File
	{
		int id = -1;
		std::string name;
		std::string path;
		std::string ext;
		std::string type;  // using strings for now,  enum binding is too much work
		size_t size;

		File()
		{
		}

		File(const cf_file_t& rawFile)
			:name(rawFile.name), path(rawFile.path),
			ext(rawFile.ext), size(rawFile.size)
		{

		}
	};

	struct Tag
	{
		int id = -1;
		std::string name;
	};

	struct FileTag
	{
		int id = -1;
		int file_id;
		int tag_id;
	};

	struct SearchPattern {
		std::string value;
	};

	using namespace sqlite_orm;
	auto storage = make_storage("./db.sqlite",

		// note:  indexing speeds up exact query but slows down like query
		//put `make_index` before `make_table` cause `sync_schema` is called in reverse order
		//make_index("idx_file_name", &File::name),

		make_table("files",
			make_column("id", &File::id, autoincrement(), primary_key()),
			make_column("name", &File::name),
			make_column("path", &File::path, unique()),
			make_column("ext", &File::ext),
			make_column("size", &File::size),
			make_column("type", &File::type)),

		make_table("tags",
			make_column("id", &Tag::id, autoincrement(), primary_key()),
			make_column("name", &Tag::name, unique())),

		make_table("fileTags",
			make_column("id", &FileTag::id, autoincrement(), primary_key()),
			make_column("file_id", &FileTag::file_id),
			make_column("tag_id", &FileTag::tag_id),
			foreign_key(&FileTag::file_id).references(&File::id),
			foreign_key(&FileTag::tag_id).references(&Tag::id)),

		make_table("searchPattern",
			make_column("searchPattern", &SearchPattern::value))
	);

	void Init()
	{
		storage.sync_schema();
	}

	void AddFile(const File& file)
	{
		//For a single column use `auto rows = storage.select(&User::id, where(...));
		auto results = storage.select(&File::id, where(is_equal(&File::path, file.path)));
		if (results.empty())
		{
			storage.insert(file);
		}
		else {
			printf("already exist [%s]\n", file.path.c_str());
		}
	}

	void AddFiles(const std::vector<cf_file_t>& files)
	{
		storage.transaction([&] {
			for (auto& file : files) {

				auto results = storage.select(&File::id, where(is_equal(&File::path, file.path)));
				if (results.empty()) {
					File f;
					f.ext = file.ext;
					f.name = file.name;
					f.path = file.path;
					f.size = file.size;
					storage.insert(f);
				}
			}
			return true;  //  commit
			});
	}

	void AddFiles(const std::vector<File>& files)
	{
		storage.transaction([&] {
			for (auto& file : files) {

				auto results = storage.select(&File::id, where(is_equal(&File::path, file.path)));
				if (results.empty()) {
					storage.insert(file);
				}
			}
			return true;  //  commit
			});
	}

	//std::vector<File> GetFilesByRoughName(const std::string& name)
	//{
	//	return storage.get_all<File>(where(like(&File::name, "%" + name + "%")));
	//}

	//std::vector<File> GetAudioFilesByRoughName(const std::string& name)
	//{
	//	return storage.get_all<File>(where(like(&File::name, "%" + name + "%") and is_equal(&File::type, AUDIO_FILE_TYPE)));
	//}

	//std::vector<File> GetTextureFilesByRoughName(const std::string& name)
	//{
	//	return storage.get_all<File>(where(like(&File::name, "%" + name + "%") and is_equal(&File::type, TEXTURE_FILE_TYPE)));
	//}



	std::vector<File> GetFilesByNameFilters(const std::string& fileType, char** tokens, int tokenCount, bool bMatchAll = true)
	{
		std::vector<File> files;

		if (tokenCount < 0)
		{
			printf("[error]: no token passed to GetTextureFilesByRoughNames()");
			return files;
		}

		storage.begin_transaction();

		for (int i = 0; i < tokenCount; i++)
		{
			const char* token = tokens[i];
			storage.insert<SearchPattern>({ token });
		}

		if (bMatchAll)
		{
			files = storage.get_all<File>(
				where(in(&File::id, select(&File::id,
					where(is_equal(&File::type, fileType) and like(&File::name, conc(conc("%", &SearchPattern::value), "%"))),
					group_by(&File::id),
					having(is_equal(count(&SearchPattern::value),
						select(count<SearchPattern>())))))));
		}
		else
		{
			files = storage.get_all<File>(
				where(in(&File::id, select(&File::id,
					where(is_equal(&File::type, fileType) and like(&File::name, conc(conc("%", &SearchPattern::value), "%")))))));
		}

		storage.rollback();

		return files;
	}

	std::vector<File> GetAudioFilesByNameFilters(char** tokens, int tokenCount, bool bMatchAll = true)
	{
		return GetFilesByNameFilters(AUDIO_FILE_TYPE, tokens, tokenCount, bMatchAll);
	}

	std::vector<File> GetTextureFilesByNameFilters(char** tokens, int tokenCount, bool bMatchAll = true)
	{
		return GetFilesByNameFilters(TEXTURE_FILE_TYPE, tokens, tokenCount, bMatchAll);
	}
}

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

	AudioPreview(const std::string& filepath)
	{
		// todo: use format specific decoing api
		const char* filepathCStr = filepath.c_str();
		decodeResult = ma_decoder_init_file(filepathCStr, NULL, &decoder);
		if (decodeResult != MA_SUCCESS) {
			printf("Failed to decode [%s].\n", filepathCStr);
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
		if (ma_device_is_started(&device))
		{
			printf("Audio already playing\n");
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

bool LoadTextureFromFile(const std::string& filename, GLuint* out_texture, int* out_width, int* out_height)
{
	// Load from file
	int image_width = 0;
	int image_height = 0;
	const char* filenameCstr = filename.c_str();
	unsigned char* image_data = stbi_load(filenameCstr, &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
	{
		printf("failed to load image: [%s]\n", filenameCstr);
		return false;
	}
	else
	{
		printf("loaded image: [%s]\n", filenameCstr);
	}

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

void ConfigImguiStyle()
{
	ImGuiStyle* style = &ImGui::GetStyle();
	ImVec4* colors = style->Colors;

	colors[ImGuiCol_Text] = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.500f, 0.500f, 0.500f, 1.000f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.180f, 0.180f, 0.180f, 1.000f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.280f, 0.280f, 0.280f, 0.000f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
	colors[ImGuiCol_Border] = ImVec4(0.266f, 0.266f, 0.266f, 1.000f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.200f, 0.200f, 0.200f, 1.000f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.280f, 0.280f, 0.280f, 1.000f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.148f, 0.148f, 0.148f, 1.000f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.160f, 0.160f, 0.160f, 1.000f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.277f, 0.277f, 0.277f, 1.000f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.300f, 0.300f, 0.300f, 1.000f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_CheckMark] = ImVec4(1.000f, 1.000f, 1.000f, 1.000f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_Button] = ImVec4(1.000f, 1.000f, 1.000f, 0.000f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
	colors[ImGuiCol_ButtonActive] = ImVec4(1.000f, 1.000f, 1.000f, 0.391f);
	colors[ImGuiCol_Header] = ImVec4(0.313f, 0.313f, 0.313f, 1.000f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
	colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.391f, 0.391f, 0.391f, 1.000f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(1.000f, 1.000f, 1.000f, 0.250f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.670f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_Tab] = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.352f, 0.352f, 0.352f, 1.000f);
	colors[ImGuiCol_TabActive] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.098f, 0.098f, 0.098f, 1.000f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.195f, 0.195f, 0.195f, 1.000f);
	//colors[ImGuiCol_preview] = ImVec4(1.000f, 0.391f, 0.000f, 0.781f);
	//colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.180f, 0.180f, 0.180f, 1.000f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.469f, 0.469f, 0.469f, 1.000f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.586f, 0.586f, 0.586f, 1.000f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(1.000f, 1.000f, 1.000f, 0.156f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_NavHighlight] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.000f, 0.391f, 0.000f, 1.000f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.586f);

	style->ChildRounding = 4.0f;
	style->FrameBorderSize = 1.0f;
	style->FrameRounding = 2.0f;
	style->GrabMinSize = 7.0f;
	style->PopupRounding = 2.0f;
	style->ScrollbarRounding = 12.0f;
	style->ScrollbarSize = 13.0f;
	style->TabBorderSize = 1.0f;
	style->TabRounding = 0.0f;
	style->WindowRounding = 4.0f;
}

static std::vector<db::File> scannedFiles;
static std::vector<db::File> filteredFiles;

static PreviewMode activeMode = PreviewMode::Texture;
static int selectedAssetIndex = -1;


void OnAssetBrowserTabSwitch()
{
	selectedAssetIndex = -1;
}

int main(int argc, char const* argv[])
{
	SDL_Window* window = NULL;
	SDL_Surface* screenSurface = NULL;

	std::unordered_map<std::string, TexturePreview> texturePreviewMap;
	std::unordered_map<std::string, AudioPreview*> audioPreviewMap;

	static char filterStr[256] = "";
	static char filterStrCopy[256] = "";
	static char* filterStrTokens[32];
	static int filterStrTokenCount = 0;

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
		io.Fonts->AddFontDefault();

		{

			// merge in icons from Font Awesome
			static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
			ImFontConfig icons_config; icons_config.MergeMode = true; icons_config.PixelSnapH = true;
			io.Fonts->AddFontFromFileTTF("./resources/fonts/" FONT_ICON_FILE_NAME_FAS, 13.0f, &icons_config, icons_ranges);
			// default font size in imgui is 13. we're setting out icon font to that as well so that it's easier to align
		}

		ConfigImguiStyle();

		io.ConfigWindowsMoveFromTitleBarOnly = true;

		bool bRunning = true;
		bool bActive = true;

		const char* assetPaths[] = {
			//"E:/Audio"
			"E:/Assets/actionrpgloot"
			/*"E:/Assets/actionrpgloot",
			"E:/Audio/RPG Sound Pack",
			"E:/Assets/armoriconpack"*/
			//"E:/Assets"
		};

		// init database
		{
			db::Init();
		}

		// load files
		{
			scannedFiles.reserve(500);
			const auto fileTraverse = [](cf_file_t* fileOnStack, void* udata)
			{
				cf_file_t rawfile = *fileOnStack;

				db::File file(rawfile);
				if (file.ext.compare(".png") == 0 ||
					file.ext.compare(".jpg") == 0)
				{
					file.type = db::TEXTURE_FILE_TYPE;
					scannedFiles.push_back(file);
				}
				else if (
					file.ext.compare(".ogg") == 0 ||
					file.ext.compare(".mp3") == 0 ||
					file.ext.compare(".wav") == 0)
				{
					file.type = db::AUDIO_FILE_TYPE;
					scannedFiles.push_back(file);
				}
			};

			for (const auto& assetPath : assetPaths)
			{
				cf_traverse(assetPath, fileTraverse, 0);
			}
		}
		db::AddFiles(scannedFiles);
		scannedFiles.clear();

		bool bFilteredForAudio = false;
		bool bFilteredForTexture = false;

		SDL_Event sdlEvent;
		while (bRunning)
		{
			while (SDL_PollEvent(&sdlEvent) != 0)
			{
				ImGui_ImplSDL2_ProcessEvent(&sdlEvent);

				switch (sdlEvent.type) {
				case SDL_KEYDOWN:
					break;

				case SDL_KEYUP:
				{
					const auto& keycode = sdlEvent.key.keysym.sym;
					if (keycode == SDLK_SPACE)
					{
						if (activeMode == PreviewMode::Audio)
						{
							if (!filteredFiles.empty() &&
								selectedAssetIndex >= 0 &&
								selectedAssetIndex < filteredFiles.size())
							{
								const auto& file = filteredFiles[selectedAssetIndex];
								if (audioPreviewMap.find(file.path) == audioPreviewMap.end())
								{
									audioPreviewMap[file.path] = new AudioPreview(file.path);
								}
								auto const audioPreview = audioPreviewMap[file.path];
								if (!audioPreview->Play()) audioPreview->Pause();
							}
						}
					}
					else if (keycode == SDLK_UP || keycode == SDLK_DOWN)
					{
						if (!filteredFiles.empty() &&
							selectedAssetIndex != -1) // only navigate when focusing on asset list
						{
							int listsize = filteredFiles.size();

							int dir = (keycode == SDLK_UP ? -1 : 1);
							selectedAssetIndex += dir;
							
							if (selectedAssetIndex < 0) selectedAssetIndex = listsize - 1;
							else if (selectedAssetIndex >= listsize) selectedAssetIndex = 0;
						}
					}
					break;
				}

				case SDL_QUIT:
					bRunning = false;
					break;

				default:
					break;
				}
			}

			// imgui begin
			{
				ImGui_ImplOpenGL3_NewFrame();
				ImGui_ImplSDL2_NewFrame(window);
				ImGui::NewFrame();
			}

			// fullscreen main view
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);

			ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar
				| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

			if (ImGui::Begin("Browser", &bActive, window_flags))
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
				{
					ImGui::BeginChild("left pane", ImVec2(200, 0), true);

					bool bFilterStrDirty = ImGui::InputText(ICON_FA_SEARCH, filterStr, IM_ARRAYSIZE(filterStr));

			

					// split tokens
					if (bFilterStrDirty)
					{
						// copy to avoid mutating og filter str for UI
						strcpy(filterStrCopy, filterStr);

						char* token = strtok(filterStrCopy, " ");
						filterStrTokenCount = 0;
						while (token != NULL)
						{
							filterStrTokens[filterStrTokenCount++] = token;
							token = strtok(NULL, " ");
						}
					}

					if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None))
					{
						if (ImGui::BeginTabItem("Texture"))
						{
							if (activeMode != PreviewMode::Texture)
							{
								activeMode = PreviewMode::Texture;
								OnAssetBrowserTabSwitch();
							}

							if (bFilterStrDirty || !bFilteredForTexture)
							{
								filteredFiles = db::GetTextureFilesByNameFilters(filterStrTokens, filterStrTokenCount);

								// todo: cleanier way to manage these states?
								{
									bFilteredForAudio = false;
									bFilteredForTexture = true;
								}
							}

							for (int i = 0; i < filteredFiles.size(); i++)
							{
								const auto& file = filteredFiles[i];
								const auto& filenameCstr = file.name.c_str();
								//if (fts::fuzzy_match_simple(filterStr, filenameCstr))
								//{
								if (ImGui::Selectable(filenameCstr, selectedAssetIndex == i))
									selectedAssetIndex = i;
								//}
							}
							ImGui::EndTabItem();

						}

						if (ImGui::BeginTabItem("Audio"))
						{
							if (activeMode != PreviewMode::Audio)
							{
								activeMode = PreviewMode::Audio;
								OnAssetBrowserTabSwitch();
							}

							if (bFilterStrDirty || !bFilteredForAudio)
							{
								filteredFiles = db::GetAudioFilesByNameFilters(filterStrTokens, filterStrTokenCount);

								// todo: cleanier way to manage these states?
								{
									bFilteredForAudio = true;
									bFilteredForTexture = false;
								}
							}

							for (int i = 0; i < filteredFiles.size(); i++)
							{
								const auto& file = filteredFiles[i];
								const auto& filenameCstr = file.name.c_str();
								/*if (fts::fuzzy_match_simple(filterStr, filenameCstr))
								{*/
								if (ImGui::Selectable(filenameCstr, selectedAssetIndex == i))
									selectedAssetIndex = i;
								//}
							}
							ImGui::EndTabItem();

						
						}
					}
					ImGui::EndTabBar();

					ImGui::EndChild();
				}
				ImGui::SameLine();

				// Right
				{
					ImGui::BeginGroup();
					ImGui::BeginChild("item view", ImVec2(0, -5 * ImGui::GetFrameHeightWithSpacing())); // Leave room for 1 line below us

					if (activeMode == PreviewMode::Texture)
					{
						if (!filteredFiles.empty() &&
							selectedAssetIndex >= 0 && selectedAssetIndex < filteredFiles.size())
						{
							const auto& file = filteredFiles[selectedAssetIndex];
							ImGui::Text("Name: %s", file.name.c_str());
							ImGui::Text("Format: %s", file.ext.c_str());
							ImGui::Text("Size: %d kb", file.size);

							// todo: maybe hash the path
							if (texturePreviewMap.find(file.path) == texturePreviewMap.end())
							{
								TexturePreview preview{};
								bool ret = LoadTextureFromFile(
									file.path,
									&preview.textureId,
									&preview.width, &preview.height);
								IM_ASSERT(ret);

								// todo: resource manager
								texturePreviewMap[file.path] = preview;
							}

							ImGui::Separator();
							if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None))
							{
								if (ImGui::BeginTabItem("Description"))
								{
									const auto& preview = texturePreviewMap[file.path];
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
						}
						ImGui::EndChild();
					}
					else if (activeMode == PreviewMode::Audio)
					{
						if (!filteredFiles.empty() &&
							selectedAssetIndex >= 0 && selectedAssetIndex < filteredFiles.size())
						{
							const auto& file = filteredFiles[selectedAssetIndex];
							ImGui::Text("Name: %s", file.name.c_str());
							ImGui::Text("Format: %s", file.ext.c_str());
							ImGui::Text("Size: %d kb", file.size);

							if (audioPreviewMap.find(file.path) == audioPreviewMap.end())
							{
								audioPreviewMap[file.path] = new AudioPreview(file.path);
							}

							auto& const audioPreview = audioPreviewMap[file.path];

							ImGui::Text("Sample Rate: %d Hz", audioPreview->decoder.outputSampleRate);
							ImGui::Text("Channel Count: %d", audioPreview->decoder.outputChannels);
							ImGui::Separator();


							const auto& buttonSize = ImVec2(30, 30);
							if (ImGui::Button(ICON_FA_PLAY, buttonSize)) {
								audioPreview->Play();
							}
							ImGui::SameLine();

							if (ImGui::Button(ICON_FA_PAUSE, buttonSize)) {
								audioPreview->Pause();
							}

							ImGui::SameLine();
							if (ImGui::Button(ICON_FA_STOP, buttonSize)) {
								audioPreview->Stop();
							}
						}
						ImGui::EndChild();
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
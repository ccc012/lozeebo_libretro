#include "libretro.h"
#include <string.h>
#include <stdbool.h>

// --- Funções Obrigatórias de Inicialização da API ---
void retro_init(void) {}
void retro_deinit(void) {}
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

// --- Identificação do seu Core no RetroArch ---
void retro_get_system_info(struct retro_system_info* info) {
	memset(info, 0, sizeof(*info));
	info->library_name = "Zeebo Core - Hello World";
	info->library_version = "0.1";
	info->valid_extensions = "mod|bin"; // Extensões de arquivo que ele vai aceitar
	info->need_fullpath = false;
}

// --- Callbacks e Funções de Ambiente (Obrigatórias mesmo vazias) ---
void retro_set_environment(retro_environment_t cb) { (void)cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { (void)cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb) { (void)cb; }
void retro_set_input_state(retro_input_state_t cb) { (void)cb; }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

void retro_reset(void) {}
void retro_run(void) {} // Onde o loop principal do jogo vai rodar futuramente

// --- Gerenciamento de Saves e Estados (Savestates) ---
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void* data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void* data, size_t size) { (void)data; (void)size; return false; }

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char* code) { (void)index; (void)enabled; (void)code; }

// --- Carga de Jogos ---
bool retro_load_game(const struct retro_game_info* game) { (void)game; return true; }
bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info) { (void)game_type; (void)info; (void)num_info; return false; }
void retro_unload_game(void) {}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void* retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
void retro_get_system_av_info(struct retro_system_av_info* info) { memset(info, 0, sizeof(*info)); }
/* midi.h — Renderer MIDI via TinySoundFont (compatível com Infuse) */
#ifndef ZEEBO_MIDI_H
#define ZEEBO_MIDI_H

#include <stdint.h>
#include <stdbool.h>

#define ZMIDI_CHANNELS 16
#define ZMIDI_MAX_NOTES 128

/* Inicializa o renderer MIDI com um soundfont .sf2 na memoria.
 * sf2_addr: endereco emulado do soundfont
 * sf2_size: tamanho em bytes do soundfont
 * sample_rate: rate de saida (ex: 44100)
 * Retorna true em sucesso. */
bool zmidi_init(uint32_t sf2_addr, uint32_t sf2_size, uint32_t sample_rate);

/* Descarrega o soundfont atual e para toda reproducao MIDI. */
void zmidi_shutdown(void);

/* Envia um byte de MIDI para o renderer (o joga escreve no port MIDI). */
void zmidi_byte(uint8_t byte);

/* Comandos MIDI de alto nivel (atalhos para o port MIDI). */
void zmidi_note_on(uint8_t ch, uint8_t note, uint8_t vel);
void zmidi_note_off(uint8_t ch, uint8_t note);
void zmidi_program_change(uint8_t ch, uint8_t program);
void zmidi_control_change(uint8_t ch, uint8_t ctrl, uint8_t val);
void zmidi_pitch_bend(uint8_t ch, int16_t bend);
void zmidi_all_notes_off(uint8_t ch);

/* Renderiza 'frames' amostras stereo intercaladas (L,R) no buffer.
 * As amostras MIDI se misturam com o que ja estiver no buffer. */
void zmidi_render(int16_t *out, uint32_t frames);
/* Carrega uma sequencia SMF (MIDI) da memoria emulada. */
bool zmidi_load_smf(const void *data, size_t size);
void zmidi_play_smf(void);
void zmidi_stop_smf(void);
bool zmidi_smf_playing(void);

/* Define volume geral MIDI (0-255). */
void zmidi_set_volume(uint8_t vol);

/* Define se o MIDI esta ativo (core option). */
void zmidi_set_active(bool active);
bool zmidi_is_active(void);

/* Informacao de uso (debug). */
uint32_t zmidi_active_notes(void);
uint32_t zmidi_voice_count(void);

/* Inicializa com soundfont padrao do disco (data/default.sf2). */
void zmidi_init_default(void);

#endif /* ZEEBO_MIDI_H */

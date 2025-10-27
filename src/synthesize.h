/***************************************************************************
 *   Copyright (C) 2005,2006 by Jonathan Duddington                        *
 *   jsd@clara.co.uk                                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#define N_PHONEME_LIST  512
#define MAX_HARMONIC  400           // 400 * 50Hz = 20 kHz, more than enough
#define N_SEQ_FRAMES   25           // max frames in a spectrum sequence (real max is ablut 8)

#define    PITCHfall   0
#define    PITCHrise   1

// flags set for frames within a spectrum sequence
#define FRFLAG_VOWEL_CENTRE    0x02   // centre point of vowel
#define FRFLAG_LEN_MOD         0x04   // reduce effect of length adjustment
#define FRFLAG_BREAK_LF        0x08   // but keep f3 upwards
#define FRFLAG_BREAK           0x10   // don't merge with next frame
#define FRFLAG_FORMANT_RATE    0x20   // Flag5 allow increased rate of change of formant freq
#define FRFLAG_MODULATE        0x40   // Flag6 modulate amplitude of some cycles to give trill

#define SFLAG_SEQCONTINUE      0x01


// formant data used by wavegen
typedef struct {
	int freq;     // Hz<<16
	int height;   // height<<15
	int left;     // Hz<<16
	int right;    // Hz<<16
	double freq1; // floating point versions of the above
	double height1;
	double left1;
	double right1;
	double freq_inc;    // increment by this every 64 samples
	double height_inc;
	double left_inc;
	double right_inc;
}  wavegen_peaks_t;


typedef struct {
	short length;
	unsigned char flags;
	unsigned char rms;
	peak_t peaks[9];
} frame_t;

typedef struct {
   short length;
   unsigned char  n_frames;
   unsigned char  flags;
   frame_t  frame[N_SEQ_FRAMES];     // max. frames in a spectrum sequence
} SPECT_SEQ;

typedef struct {
	short length;
	unsigned char flags;
	frame_t *frame;
} frameref_t;


typedef struct {
	PHONEME_TAB *ph;
	unsigned char env;    // pitch envelope number
	unsigned char tone;
	unsigned char type;
	unsigned char prepause;
	unsigned char amp;
	unsigned char flags;
	unsigned char newword;   // 1=start of word, 2=end of clause
	unsigned char sflags;
	short length;  // length_mod
	short pitch1;  // pitch, 0-4095 within the Voice's pitch range
	short pitch2;
	short sourceix;  // ix into the original source text string, only set at the start of a word
} PHONEME_LIST;


// phoneme table
extern PHONEME_TAB *phoneme_tab;

// list of phonemes in a clause
extern int n_phoneme_list;
extern PHONEME_LIST phoneme_list[N_PHONEME_LIST];

extern unsigned char Pitch_env0[128];


// queue of commands for wavegen
#define WCMD_AMPLITUDE 1
#define WCMD_PITCH	2
#define WCMD_SPECT	3
#define WCMD_SPECT2	4
#define WCMD_PAUSE	5
#define WCMD_WAVE    6
#define WCMD_WAVE2   7
#define WCMD_MARKER	8		// not yet used
#define WCMD_VOICE   9		// not yet used

#define N_WCMDQ   200
#define MIN_WCMDQ  20   // need this many free entries before adding new phoneme

extern int wcmdq[N_WCMDQ][5];
extern int wcmdq_head;
extern int wcmdq_tail;

// from Wavegen file
extern int WcmdqFree();
extern void WcmdqStop();
extern int WcmdqUsed();
extern void WcmdqInc();
extern int WavegenOpenSound();
extern int WavegenCloseSound();
extern int	WavegenInitSound();
extern void WavegenInit(int rate, int wavemult_fact);
extern int OpenWaveFile(const char *path);
extern void CloseWaveFile();

extern float polint(float xa[],float ya[],int n,float x);
extern void WavegenSetEcho(int delay, int amp);
extern unsigned char *wavefile_data;
extern int samplerate;

extern int wavefile_ix;
extern int WavegenFile(void);


// from synthdata file
extern unsigned int LookupSound(PHONEME_TAB *ph1, PHONEME_TAB *ph2, int which, int *match_level);
extern frameref_t *LookupSpect(PHONEME_TAB *ph1, PHONEME_TAB *ph2, int which, int *match_level, int *n_frames, int stress);
extern int LoadPhData();



class Synthesize
{//=============
public:
	Synthesize();
	~Synthesize();
	int Generate(PHONEME_LIST *phoneme_list, int resume);
	void MakeWave2(PHONEME_LIST *p, int n_ph);
	int OnTimer(void);
	void SpeakNextClause(FILE *f_text, int stop);
	int Status(void);

private:
	void DoAmplitude(int amp);
	void DoPitch(unsigned char *env, int pitch1, int pitch2);
	void DoPause(int length);
	void DoSample(PHONEME_TAB *ph1, PHONEME_TAB *ph2, int which, int length);
	void DoSample2(int index, int which, int length_mod);
	void DoSpect(PHONEME_TAB *this_ph, PHONEME_TAB *this_ph, int which, int length_mod, int stress, int modulation);
	void DoMarker(int type, int index);
	void EndPitch(int voice_break);
	void SmoothSpect(void);
	void StartSyllable(void);

	int  last_pitch_cmd;
	frame_t  *last_frame;
	int  last_wcmdq;
	int  pitch_length;

	int  syllable_start;
	int  syllable_end;
	int  syllable_centre;
	int  smoothing_factor;
};


extern Synthesize *synth;
extern char *spects_data;
extern unsigned char *envelope_data[16];


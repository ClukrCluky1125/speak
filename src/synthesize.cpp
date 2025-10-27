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

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "speech.h"
#include "phoneme.h"
#include "synthesize.h"
#include "translate.h"

#define    PITCHfall   0
#define    PITCHrise   1

Synthesize *synth;

const char *tempwav = "speech.wav";
//#define LOG_WGEN

extern FILE *f_log;


// list of phonemes in a clause
int n_phoneme_list=0;
PHONEME_LIST phoneme_list[N_PHONEME_LIST];


int speed_factor1;
int speed_factor2;

// limit the rate of change for each formant number
float formant_rate[9] = {0.2, 0.4, 0.64, 0.9, 0.85, 0.85, 1, 1, 1};


#define RMS1  15  // 12 - 15
#define RMS2  20  // 16 - 20
#define RMS_START  25  // 14 - 30
#define VOWEL_FRONT_LENGTH  50



// a dummy phoneme_list entry which looks like a pause
static PHONEME_LIST next_pause;


const char *WordToString(unsigned int word)
{//========================================
// Convert a phoneme mnemonic word into a string
	int  ix;
	static char buf[5];

	for(ix=0; ix<3; ix++)
		buf[ix] = word >> (ix*8);
	buf[4] = 0;
	return(buf);
}


void StartLog(int on)
{//==================
#ifdef LOG_WGEN
	if(f_log == NULL)
		f_log=fopen("speech.log","w");
#endif
}


#ifdef LOG_SYNTH
void LogPitch(PHONEME_LIST *p)
{//===========================
	int x;
	int pitch1 = p->pitch1;
	int pitch2 = p->pitch2;
	float pitch_base;
	float pitch_range;

return;	
	if(pitch1 > pitch2)
	{
		x = pitch1;   // swap values
		pitch1 = pitch2;
		pitch2 = x;
	}

	pitch_base = float(voice->pitch_base + (pitch1 * voice->pitch_range))/4096;
	pitch_range = float(voice->pitch_base + (pitch2 * voice->pitch_range) - pitch_base)/4096;
	fprintf(f_logs,"%2s  %d  %5.1f %5.1f\n",WordToString(p->ph->mnemonic),p->env,pitch_base,pitch_range);
	fflush(f_logs);
}
#endif


void PlayWavFile(const char *fname)
{//================================
	char command[120];

	sprintf(command,"play %s",fname);
	system(command);
}


Synthesize::Synthesize()
{//=====================
	last_pitch_cmd = 0;
	last_frame = NULL;
	syllable_centre = -1;
	smoothing_factor = 0;

	// initialise next_pause, a dummy phoneme_list entry
	next_pause.ph = &phoneme_tab[phonPAUSE];
	next_pause.type = phPAUSE;
	next_pause.newword = 1;
}

Synthesize::~Synthesize()
{//======================
}


void Synthesize::EndPitch(int voice_break)
{//=======================================
	// posssible end of pitch envelope, fill in the length
	if(pitch_length > 0)
	{
		if(wcmdq[last_pitch_cmd][1] == 0)
			wcmdq[last_pitch_cmd][1] = pitch_length;
		pitch_length = 0;
	}

	if(voice_break)
	{
		last_wcmdq = -1;
		last_frame = NULL;
		syllable_end = wcmdq_tail;
		SmoothSpect();
		syllable_centre = -1;
	}
}  // end of Synthesize::EndPitch



void Synthesize::DoAmplitude(int amp)
{//==================================
	wcmdq[wcmdq_tail][0] = WCMD_AMPLITUDE;
	wcmdq[wcmdq_tail][2] = amp;
	WcmdqInc();
}  // end of Synthesize::DoAmplitude



void Synthesize::DoPitch(unsigned char *env, int pitch1, int pitch2)
{//=================================================================
	int *q;

	EndPitch(0);
	
	last_pitch_cmd = wcmdq_tail;
	pitch_length = 0;       // total length of spect with this pitch envelope
	
	q = wcmdq[wcmdq_tail];
	q[0] = WCMD_PITCH;
	q[1] = 0;   // length, fill in later from pitch_length
	q[2] = (int)env;
	q[3] = pitch1;
	q[4] = pitch2;
	WcmdqInc();
}  //  end of Synthesize::DoPitch



void Synthesize::DoPause(int length)
{//=================================
	float len;

	len = float(length) * samplerate * speed_factor1;

	EndPitch(1);
	wcmdq[wcmdq_tail][0] = WCMD_PAUSE;
	wcmdq[wcmdq_tail][1] = int(len/256000);
	WcmdqInc();
	last_frame = NULL;
}  // end of Synthesize::DoPause



void Synthesize::DoSample(PHONEME_TAB *ph1, PHONEME_TAB *ph2, int which, int length_mod)
{//=====================================================================================
	int index;
	int match_level;

	EndPitch(1);
	index = LookupSound(ph1,ph2,which & 0xff,&match_level);
	if((index & 0x800000) == 0)
		return;             // not wavefile data
	
	DoSample2(index,which,length_mod);
	last_frame = NULL;
}  // end of Synthesize::DoSample



void Synthesize::DoSample2(int index, int which, int length_mod)
{//=============================================================
	int length;
	int length1;
	int format;
	int *q;
	unsigned char *p;

	index = index & 0x7fffff;
	p = &wavefile_data[index];
	format = p[2];
	length1 = (p[1] * 256);
	length1 += p[0];    //  length in bytes
	if(length_mod > 0)
		length = (length1 * length_mod) / 256;
	else
		length = length1;

	length = (length * speed_factor2)/256;
	if(length > length1)
		length = length1;  // don't exceed wavefile length

	if(format==0)
		length /= 2;     // 2 byte samples

	index += 4;
	
	last_wcmdq = wcmdq_tail;
	q = wcmdq[wcmdq_tail];
	if(which & 0x100)
		q[0] = WCMD_WAVE2;    // mix this with synthesised wave
	else
		q[0] = WCMD_WAVE;
	q[1] = length;   // length in samples
	q[2] = int(&wavefile_data[index]);
	q[3] = format;
	WcmdqInc();

}  // end of Synthesize::DoSample2


frame_t *AllocFrame()
{//=================
	// Allocate a temporary spectrum frame for the wavegen queue. Use a pool which is big
	// enough to use a round-robin without checks.
	// Only needed for modifying spectra for blending to consonants

#define N_FRAME_POOL  N_WCMDQ
	static int ix=0;
	static frame_t frame_pool[N_FRAME_POOL];
	
	ix++;
	if(ix >= N_FRAME_POOL)
		ix = 0;
	return(&frame_pool[ix]);
}


void set_frame_rms(frame_t *fr, int new_rms)
{//=========================================
// Each frame includes its RMS amplitude value, so to set a new
// RMS just adjust the formant amplitudes by the appropriate ratio

	double x;
	double h;
	int ix;
	
	if(fr->rms == 0) return;    // check for divide by zero
	x = double(new_rms);
	x = sqrt(x / fr->rms);
	
	for(ix=0; ix<9; ix++)
	{
		h = (double)fr->peaks[ix].pkheight;
		fr->peaks[ix].pkheight = int(h * x);
	}
}   /* end of set_frame_rms */


void formants_reduce_hf(frame_t *fr, int level)
{//============================================
//  change height of peaks 2 to 8 to level/128 
	int  ix;
	int  x;
	
	for(ix=2; ix<9; ix++)
	{
		x = fr->peaks[ix].pkheight * level;
		fr->peaks[ix].pkheight = x >> 7;
	}
}


frame_t *CopyFrame(frame_t *frame1)
{//================================
//  create a copy of the specified frame in temporary buffer
	frame_t *frame2;
	
	frame2 = AllocFrame();
	if(frame2 != NULL)
	{	
		memcpy(frame2,frame1,sizeof(frame_t));
		frame2->length = 0;
	}
	return(frame2);
}


frame_t *DuplicateLastFrame(frameref_t *seq, int n_frames, int length)
{//===================================================================
	frame_t *fr;

	seq[n_frames-1].length = length;
	fr = CopyFrame(seq[n_frames-1].frame);
	seq[n_frames].frame = fr;
	seq[n_frames].length = 0;
	return fr;
}


void AdjustFormants(frame_t *fr, int target, int min, int max, int f1_adj, int f3_adj, int hf_reduce)
{//==================================================================================================
	int x;
	
	x = (target - fr->peaks[2].pkfreq) / 2;
	if(x > max) x = max;
	if(x < min) x = min;
	fr->peaks[2].pkfreq += x;
	fr->peaks[3].pkfreq += f3_adj;
	fr->peaks[4].pkfreq += f3_adj;
	fr->peaks[5].pkfreq += f3_adj;
	
	if(f1_adj)
	{
		x = (235 - fr->peaks[1].pkfreq);
		if(x < -300) x = -300;
		if(x > -150) x = -150;
		fr->peaks[1].pkfreq += x;
		fr->peaks[0].pkfreq += x;
	}
	formants_reduce_hf(fr,70);       // ?? using fixed amount rather than the parameter??
}


void BendVowel(frameref_t *seq, int &n_frames, PHONEME_TAB *other_ph, int which)
{//=============================================================================
// Adjust the beginning (which=1) and end (which=2) of a vowel, depending on the
// adjacent phoneme.

// Note: It would be better for this function to look at the adacent phoneme's properties
// (type and place of articulation) rather than its mnemonic
	
	unsigned int  ph_name;
	frame_t *fr;
	
	if(n_frames < 2)
		return;

	ph_name = other_ph->mnemonic;
	
	if(which == 1)
	{
		/* entry to vowel */
		fr = CopyFrame(seq[0].frame);
		seq[0].frame = fr;
		seq[0].length = VOWEL_FRONT_LENGTH;
		seq[0].flags |= FRFLAG_LEN_MOD;              // reduce length modification
		fr->flags |= FRFLAG_LEN_MOD;

		switch(ph_name)
		{
		case 'v':
			AdjustFormants(fr,1000,-300,-200,0,-300,128);
			set_frame_rms(fr,RMS_START);
			break;

		case 'b':
			break;

		case (('-'*256) + 'g'):
		case 'g':
		case (('Z'*256) + 'd'):
			AdjustFormants(fr,2300,300,400,1,-100,100);
			set_frame_rms(fr,RMS_START);
			break;

		case 'd':
			AdjustFormants(fr,1700,-300,300,1,-100,100);
			set_frame_rms(fr,RMS_START);
			break;

//		case 'p':
		case 'm':
//		case 'f':
			AdjustFormants(fr,1000,-300,-100,0,-300,128);
			set_frame_rms(fr,14);
			break;

		case 'n':
			AdjustFormants(fr,1700,-300,300,0,-100,100);
			set_frame_rms(fr,14);
			break;

		case 't':
		case 'T':
		case 'C':
		case 's':
			AdjustFormants(fr,1700,-300,300,0,-100,100);
			set_frame_rms(fr,RMS_START);
			break;

		case 'k':
		case 'N':
		case 'x':
		case 'S':
		case (('S'*256) + 't'):
			AdjustFormants(fr,2300,300,400,0,-100,100);
			set_frame_rms(fr,RMS_START);
			break;

		default:
			set_frame_rms(fr,RMS_START);
			break;
		}
	}
	else
	{
		/* exit from vowel */
		switch(ph_name)
		{
		case 'R':
			fr = CopyFrame(seq[n_frames-2].frame);
			seq[n_frames-2].frame = fr;
			fr->flags |= (FRFLAG_MODULATE);
			break;

		case ';':      // pause
		case '_':
			fr = DuplicateLastFrame(seq,n_frames++,50);
			set_frame_rms(fr,RMS1);
			break;

		case 'v':
			fr = DuplicateLastFrame(seq,n_frames++,50);
			AdjustFormants(fr,1000,-500,-300,0,-300,80);
			set_frame_rms(fr,RMS1);
			break;
			
		case 'p':
		case 'f':
		case 'm':
			fr = DuplicateLastFrame(seq,n_frames++,35);
			AdjustFormants(fr,1000,-500,-300,0,-300,100);
			set_frame_rms(fr,RMS2);
			break;
			
		case 'D':
		case 'z':
			fr = DuplicateLastFrame(seq,n_frames++,50);
			AdjustFormants(fr,1700,-300,300,0,-100,80);
			set_frame_rms(fr,RMS1);
			break;

		case 't':
		case 's':
		case 'T':
		case 'C':
		case 'n':
			fr = DuplicateLastFrame(seq,n_frames++,35);
			AdjustFormants(fr,1700,-300,300,0,-100,100);
			set_frame_rms(fr,RMS2);
			break;

		case 'k':
		case 'x':
//		case 'N':
			fr = DuplicateLastFrame(seq,n_frames++,35);
			AdjustFormants(fr,2300,300,400,0,-100,100);
			set_frame_rms(fr,RMS2);
			break;

		case 'Z':
		case (('Z'*256) + 'd'):
		case 'g':
			fr = DuplicateLastFrame(seq,n_frames++,35);
			fr->flags |= FRFLAG_BREAK;       // don't merge with next frame
			
			AdjustFormants(fr,2300,300,400,1,-100,100);
			set_frame_rms(fr,RMS1);
			break;

		case 'b':
			fr = DuplicateLastFrame(seq,n_frames++,35);
			fr->flags |= FRFLAG_BREAK;       // don't merge with next frame
			
			AdjustFormants(fr,1000,-500,-300,1,-300,100);
			set_frame_rms(fr,RMS1);
			break;
			
		case 'd':
			fr = DuplicateLastFrame(seq,n_frames++,35);
			fr->flags |= FRFLAG_BREAK;       // don't merge with next frame
			
			AdjustFormants(fr,1700,-300,300,1,-100,100);
			set_frame_rms(fr,RMS1);
			break;
			
		}
		if(other_ph->type == phNASAL)
		{
			// duplicate last frame to prevent merging to next sequence 
			DuplicateLastFrame(seq,n_frames++,0);
		}
	}
}   //  end of Bend Vowel



void Synthesize::SmoothSpect(void)
{//===============================
	// Limit the rate of frequence change of formants, to reduce chirping

	int *q;
	frame_t *frame;
	frame_t *frame2;
	frame_t *frame1;
	frame_t *frame_centre;
	int ix;
	int len;
	int pk;
	int modified;
	int allowed;
	int diff;

	if(syllable_start == syllable_end)
		return;

	if((syllable_centre < 0) || (syllable_centre == syllable_start))
	{
		syllable_start = syllable_end;
		return;
	}

	q = wcmdq[syllable_centre];
	frame_centre = (frame_t *)(q[2]-4);

	// backwards
	ix = syllable_centre -1;
	frame = frame2 = frame_centre;
	for(;;)
	{
		if(ix < 0) ix = N_WCMDQ-1;
		q = wcmdq[ix];

		if(q[0] == WCMD_PAUSE || q[0] == WCMD_WAVE)
			break;

		if(q[0] == WCMD_SPECT || q[0] == WCMD_SPECT2)
		{
	
			len = q[1] & 0xffff;
	
			frame1 = (frame_t *)(q[3]-4);
			if(frame1 == frame)
			{
				q[3] = (int)frame2->peaks;
				frame1 = frame2;
			}
			else
				break;  // doesn't follow on from previous frame
	
			frame = frame2 = (frame_t *)(q[2]-4);
			modified = 0;

			if(frame->flags & FRFLAG_FORMANT_RATE)
				len = (len * 6)/5;      // allow slightly greater rate of change for this frame

			for(pk=0; pk<6; pk++)
			{
				allowed = int(formant_rate[pk] * len);
		
				diff = frame->peaks[pk].pkfreq - frame1->peaks[pk].pkfreq;
				if(diff > allowed)
				{
					if(modified == 0)
					{
						frame2 = CopyFrame(frame);
						modified = 1;
					}
					frame2->peaks[pk].pkfreq = frame1->peaks[pk].pkfreq + allowed;
					q[2] = (int)frame2->peaks;
				}
				else
				if(diff < -allowed)
				{
					if(modified == 0)
					{
						frame2 = CopyFrame(frame);
						modified = 1;
					}
					frame2->peaks[pk].pkfreq = frame1->peaks[pk].pkfreq - allowed;
					q[2] = (int)frame2->peaks;
				}
			}
		}

		if(ix == syllable_start)
			break;
		ix--;
	}

	// forwards
	ix = syllable_centre;

	frame = NULL;
	for(;;)
	{
		q = wcmdq[ix];

		if(q[0] == WCMD_PAUSE || q[0] == WCMD_WAVE)
			break;

		if(q[0] == WCMD_SPECT || q[0] == WCMD_SPECT2)
		{
	
			len = q[1] & 0xffff;
	
			frame1 = (frame_t *)(q[2]-4);
			if(frame != NULL)
			{
				if(frame1 == frame)
				{
					q[2] = (int)frame2->peaks;
					frame1 = frame2;
				}
				else
					break;  // doesn't follow on from previous frame
			}
	
			frame = frame2 = (frame_t *)(q[3]-4);
			modified = 0;

			if(frame->flags & FRFLAG_FORMANT_RATE)
				len = (len *6)/5;      // allow slightly greater rate of change for this frame

			for(pk=0; pk<6; pk++)
			{
				allowed = int(formant_rate[pk] * len);
		
				diff = frame->peaks[pk].pkfreq - frame1->peaks[pk].pkfreq;
				if(diff > allowed)
				{
					if(modified == 0)
					{
						frame2 = CopyFrame(frame);
						modified = 1;
					}
					frame2->peaks[pk].pkfreq = frame1->peaks[pk].pkfreq + allowed;
					q[3] = (int)frame2->peaks;
				}
				else
				if(diff < -allowed)
				{
					if(modified == 0)
					{
						frame2 = CopyFrame(frame);
						modified = 1;
					}
					frame2->peaks[pk].pkfreq = frame1->peaks[pk].pkfreq - allowed;
					q[3] = (int)frame2->peaks;
				}
			}
		}

		ix++;
		if(ix >= N_WCMDQ) ix = 0;
		if(ix == syllable_end)
			break;
	}

	syllable_start = syllable_end;
}


void Synthesize::StartSyllable(void)
{//=================================
	// start of syllable, if not already started
	if(syllable_end == syllable_start)
		syllable_end = wcmdq_tail;
}


void Synthesize::DoSpect(PHONEME_TAB *this_ph, PHONEME_TAB *other_ph, int which, int length_mod, int stress, int modulation)
{//=============================================================================================
	// which  1  start of phoneme,   2 body and end
	// length_mod: 256 = 100%

	int  n_frames;
	frameref_t *frames;
	int  frameix;
	frame_t *frame1;
	frame_t *frame2;
	frame_t *fr;
	int  *q;
	int  len;
	int  match_level;
	int  frame_length;
	int  frame1_length;
	int  frame2_length;
	float xlength;
	float length_factor;
	static int wave_flag = 0;
	int wcmd_spect = WCMD_SPECT;


	if(length_mod==0) length_mod=256;

	frames = LookupSpect(this_ph,other_ph,which,&match_level,&n_frames, stress);
	if(frames == NULL)
		return;   // not found

	if(wavefile_ix == 0)
	{
		if(wave_flag)
		{
			// cancel any wavefile that was playing previously
			wcmd_spect = WCMD_SPECT2;
			wave_flag = 0;
		}
		else
		{
			wcmd_spect = WCMD_SPECT;
		}
	}

	frame1 = frames[0].frame;
	frame1_length = frames[0].length;
	if(last_frame != NULL)
	{
		if((last_frame->length < 2) && !(last_frame->flags & FRFLAG_BREAK))
		{
			// last frame of previous sequence was zero-length, replace with first of this sequence
			wcmdq[last_wcmdq][3] = (int)frame1->peaks;

			if(last_frame->flags & FRFLAG_BREAK_LF)
			{
				// but flag indicates keep HF peaks in last segment
				fr = CopyFrame(frame1);
				memcpy(&fr->peaks[3],&last_frame->peaks[3],sizeof(peak_t)*6);
				wcmdq[last_wcmdq][3] = (int)fr->peaks;
			}
		}
	}
	
	if((this_ph->type == phVOWEL) && (which == 2))
	{
		SmoothSpect();    // process previous syllable

		// remember the point in the output queue of the centre of the vowel
		syllable_centre = wcmdq_tail;
	}

	frame_length = frame1_length;
	for(frameix=1; frameix<n_frames; frameix++)
	{
		frame2 = frames[frameix].frame;
		frame2_length = frames[frameix].length;
		
		if(wavefile_ix != 0)
		{
			// there is a wave file to play along with this synthesis
			DoSample2(wavefile_ix,which+0x100,0);
			wave_flag = 1;
			wavefile_ix = 0;
		}
 
		length_factor = float(length_mod)/256;
		if(frame1->flags & FRFLAG_LEN_MOD)     // reduce effect of length mod
		{
			length_factor = float(length_mod + 256)/512;
		}
		if(frame1->flags & FRFLAG_MODULATE)
		{
			modulation = 3;
		}
		xlength = (frame_length * samplerate * length_factor)/1000;

#ifdef deleted
if((which==2) && (other_ph->mnemonic == 'R'))
{
	// try this: lengthen last (modulated) frame of vowel before [R]
	if(frameix == n_frames-1) 
		xlength = xlength + (1200 * speed_factor2)/256;
}
#endif
		len = int(xlength);
		pitch_length += len;

		if(frame_length < 2)
		{
			last_frame = NULL;
			frame_length = frame2_length;
			frame1 = frame2;
		}
		else
		{
			last_wcmdq = wcmdq_tail;
			q = wcmdq[wcmdq_tail];
			q[0] = wcmd_spect;
			q[1] = len + (modulation << 16);
			q[2] = int(frame1->peaks);
			q[3] = int(frame2->peaks);
			WcmdqInc();
			last_frame = frame1 = frame2;
			frame_length = frame2_length;
		}
	}
}  // end of Synthesize::DoSpect


void Synthesize::DoMarker(int type, int index)
{//===========================================
// not currently used

}  // end of Synthesize::DoMarker



int Synthesize::Generate(PHONEME_LIST *phoneme_list, int resume)
{//=============================================================
	static int  ix;
	PHONEME_LIST *prev;
	PHONEME_LIST *next;
	PHONEME_LIST *next2;
	PHONEME_LIST *p;
	int  released;
	int  modulation;
	int  pre_voiced;
	PHONEME_TAB *ph;

	if(resume == 0)
	{
		ix = 1;
		pitch_length = 0;
		last_frame = NULL;
		last_wcmdq = -1;
		syllable_start = wcmdq_tail;
		syllable_end = wcmdq_tail;
		syllable_centre = -1;
	}
	
	while(ix<n_phoneme_list)
	{
		if(WcmdqFree() <= MIN_WCMDQ)
			return(1);  // wait

		prev = &phoneme_list[ix-1];
		p = &phoneme_list[ix];
		next = &phoneme_list[ix+1];
		next2 = &phoneme_list[ix+2];

		if(p->newword)
			last_frame = NULL;

		if(option_words > 0)
		{
			// prevent word merging into next, make it look as though next is a pause
			if((next->newword) && (next->type != phPAUSE))
				next = &next_pause;
		}

		if(p->prepause > 0)
			DoPause(p->prepause);
			
		switch(p->type)
		{
		case phPAUSE:
			DoPause(p->length);
			break;
			
		case phSTOP:
			released = 0;
			if(next->type==phVOWEL) released = 1;
			if(next->type==phLIQUID && !next->newword) released = 1;

			if(released)
				DoSample(p->ph,next->ph,2,0);
			else
				DoSample(p->ph,&phoneme_tab[phonPAUSE],2,0);
			break;
			
		case phFRICATIVE:
			DoSample(p->ph,next->ph,2,p->length);
			break;

		case phVSTOP:
			pre_voiced = 0;
			if(next->type==phVOWEL)
			{
				DoAmplitude(p->amp);
				DoPitch(envelope_data[p->env],p->pitch1,p->pitch2);
				pre_voiced = 1;
#ifdef LOG_SYNTH
	LogPitch(p);
#endif
			}
			else
			if((next->type==phLIQUID) && !next->newword)
			{
				DoAmplitude(next->amp);
				DoPitch(envelope_data[next->env],next->pitch1,next->pitch2);
				pre_voiced = 1;
#ifdef LOG_SYNTH
	LogPitch(next);
#endif
			}
			if(prev->type==phVOWEL)
			{
				DoSpect(p->ph,&phoneme_tab[phonSCHWA],1,p->length,0,0);
			}

			if(pre_voiced)
			{
				StartSyllable();
				DoSpect(p->ph,next->ph,2,p->length,0,0);
			}
			else
				DoSpect(p->ph,&phoneme_tab[phonPAUSE],2,p->length,0,0);
			break;

		case phVFRICATIVE:
			if(next->type==phVOWEL)
			{
				DoAmplitude(p->amp);
				DoPitch(envelope_data[p->env],p->pitch1,p->pitch2);
#ifdef LOG_SYNTH
	LogPitch(p);
#endif
			}
			else
			if(next->type==phLIQUID)
			{
				DoAmplitude(next->amp);
				DoPitch(envelope_data[next->env],next->pitch1,next->pitch2);
#ifdef LOG_SYNTH
	LogPitch(next);
#endif
			}
			if((next->type==phVOWEL) || (next->type==phLIQUID))
			{
				StartSyllable();
				DoSpect(p->ph,next->ph,2,p->length,0,0);
			}
			else
				DoSpect(p->ph,&phoneme_tab[phonPAUSE],2,p->length,0,0);
			break;

		case phNASAL:
			if(!(p->sflags & SFLAG_SEQCONTINUE))
			{
				DoAmplitude(p->amp);
				DoPitch(envelope_data[p->env],p->pitch1,p->pitch2);
#ifdef LOG_SYNTH
	LogPitch(p);
#endif
			}

			if(prev->type==phNASAL)
			{
				last_frame = NULL;
			}

			if(next->type==phVOWEL)
			{
				StartSyllable();
				DoSpect(p->ph,prev->ph,1,p->length,0,0);  // (,)r
			}
			else
			if(prev->type==phVOWEL && (p->sflags & SFLAG_SEQCONTINUE))
			{
				DoSpect(p->ph,&phoneme_tab[phonPAUSE],2,p->length,0,0);
			}
			else
			{
				last_frame = NULL;  // only for nasal ?
				if(next->type == phLIQUID)
					DoSpect(p->ph,&phoneme_tab[phonSONORANT],2,p->length,0,0);
				else
					DoSpect(p->ph,&phoneme_tab[phonPAUSE],2,p->length,0,0);
				last_frame = NULL;
			}

			break;

		case phLIQUID:
			modulation = 0;
			if(p->ph->flags & phMODULATE)
				modulation = 3;

			if(!(p->sflags & SFLAG_SEQCONTINUE))
			{
				DoAmplitude(p->amp);
				DoPitch(envelope_data[p->env],p->pitch1,p->pitch2);
#ifdef LOG_SYNTH
	LogPitch(p);
#endif
			}

			if(prev->type==phNASAL)
			{
				last_frame = NULL;
			}

			if(next->type==phVOWEL)
			{
				StartSyllable();
				DoSpect(p->ph,prev->ph,1,p->length,0,modulation);  // (,)r
			}
			else
			if(prev->type==phVOWEL && (p->sflags & SFLAG_SEQCONTINUE))
			{
				DoSpect(p->ph,prev->ph,1,p->length,0,modulation);
			}
			else
			{
				last_frame = NULL;
				DoSpect(p->ph,next->ph,2,p->length,0,modulation);
				last_frame = NULL;
			}

			break;

		case phVOWEL:
			ph = p->ph;
			
			StartSyllable();

			modulation = 0;
			if(p->tone <= 1)
				modulation = 2;
			else
			if(p->tone >= 7)
				modulation = 2;

			if(prev->type == phVSTOP || prev->type == phVFRICATIVE)
			{
				DoAmplitude(p->amp);
				DoPitch(envelope_data[p->env],p->pitch1,p->pitch2);  // don't use prevocalic rising tone
				DoSpect(ph,prev->ph,1,p->length,p->tone,modulation); 
#ifdef LOG_SYNTH
	LogPitch(p);
#endif
			}
			else
			if(prev->type==phLIQUID || prev->type==phNASAL)
			{
				DoAmplitude(p->amp);
				DoSpect(ph,prev->ph,1,p->length,p->tone,modulation);   // continue with pre-vocalic rising tone
				DoPitch(envelope_data[p->env],p->pitch1,p->pitch2);
#ifdef LOG_SYNTH
	LogPitch(p);
#endif
			}
			else
			{
				if(!(p->sflags & SFLAG_SEQCONTINUE))
				{
					DoAmplitude(p->amp);
					DoPitch(envelope_data[p->env],p->pitch1,p->pitch2);
				}

				DoSpect(ph,prev->ph,1,p->length,p->tone,modulation);
			}
//			if(next->newword)
//				DoSpect(p->ph,&phoneme_tab[phonPAUSE],2,p->length);
//			else
				DoSpect(p->ph,next->ph,2,p->length,p->tone,modulation);
			break;
		}
		ix++;
	}
	EndPitch(1);
	return(0);  // finished the phoneme list
}  //  end of Generate



void Synthesize::MakeWave2(PHONEME_LIST *p, int n_ph)
{//==================================================
	int result;
	
	OpenWaveFile(tempwav);

	Generate(p,0);

	for(;;)
	{
		result = WavegenFile();
		if(result != 0)
			break;
		Generate(p,1);
	}
	
	CloseWaveFile();
	PlayWavFile(tempwav);

}  // end of Synthesize::MakeWave2




int timer_on = 0;
int paused = 0;

int Synthesize::OnTimer()
{//======================
	
	if(!timer_on)
	{
		return(WavegenCloseSound());
	}

	if(Generate(phoneme_list,1)==0)
		SpeakNextClause(NULL,0);
		
	return(0);
}


int Synthesize::Status()
{//=====================
	return(timer_on | paused);
}

void Synthesize::SpeakNextClause(FILE *f_in, int stop)
{//===================================================
// stop 1: stop
//      2: pause

	int tone;
	static FILE *f_text=NULL;

	if(stop == 1)
	{
		timer_on = 0;
		if(f_text != NULL)
		{
			fclose(f_text);
			f_text=NULL;
		}
		n_phoneme_list = 0;
		WcmdqStop();
		return;
	}
	if(stop == 2)
	{
		if(paused == 0)
		{
			timer_on = 0;
			paused = 2;
		}
		else
		{
			WavegenOpenSound();
			timer_on = 1;
			paused = 0;
			Generate(phoneme_list,0);   // re-start from beginning of clause
		}
		return;
	}

	if(f_in != NULL)
	{
		WavegenSetEcho(option_echo_delay,option_echo_amp);
		f_text = f_in;
		timer_on = 1;
		paused = 0;
	}
	if(f_text==NULL)
		return;
	if(feof(f_text))
	{
		timer_on = 0;
		fclose(f_text);
		f_text=NULL;
		return;
	}
	
	// read the next clause from the input text file, translate it, and generate
	// entries in the wavegen command queue
	translator->TranslateClause(f_text,NULL,&tone);
	if(option_phonemes)
	{
		printf("%s\n",translator->phon_out);
	}
	translator->CalcPitches(tone);
	translator->CalcLengths();
	Generate(phoneme_list,0);
	WavegenOpenSound();
}  //  end of SpeakNextClause


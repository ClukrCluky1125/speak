/***************************************************************************
 *   Copyright (C) 2005 by Jonathan Duddington                             *
 *   jsd1@clara.co.uk                                                      *
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


typedef struct {
	// pitch
	int pitch_base;    // Hz<<12
	int pitch_range;   // standard = 0x1000
	
	float speedf1;
	float speedf2;
	float speedf3;

	int intonation1;
	int intonation2;
	// percentages shown to user, ix=N_PEAKS means ALL peaks
	USHORT pcnt[N_PEAKS+1][3];

	// parameters used by Wavegen
	int freq[N_PEAKS];    // 100% = 0x100
	int height[N_PEAKS];  // 100% = 0x100
	int width[N_PEAKS];   // 100% = 0x100
} voice_t;


extern voice_t *voice;

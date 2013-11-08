// Blip_Buffer 0.4.1. http://www.slack.net/~ant/

#include <blip/Blip_Buffer.h>

#include <assert.h>
#include <climits>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Copyright (C) 2003-2006 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

#ifdef BLARGG_ENABLE_OPTIMIZER
	#include BLARGG_ENABLE_OPTIMIZER
#endif

Blip_Buffer::Blip_Buffer()
{
	factor_       = (blip_u64)ULLONG_MAX;
	offset_       = 0;
	buffer_       = 0;
	buffer_size_  = 0;
	sample_rate_  = 0;
	reader_accum_ = 0;
	bass_shift_   = 0;
	clock_rate_   = 0;
	bass_freq_    = 16;
	length_       = 0;
}

Blip_Buffer::~Blip_Buffer()
{
   if (buffer_)
      free( buffer_ );
}

void Blip_Buffer::clear( int entire_buffer )
{
	offset_      = 0;
	reader_accum_ = 0;
	modified_    = 0;
	if ( buffer_ )
	{
		long count = (entire_buffer ? buffer_size_ : samples_avail());
		memset( buffer_, 0, (count + blip_buffer_extra_) * sizeof (buf_t_) );
	}
}

Blip_Buffer::blargg_err_t Blip_Buffer::set_sample_rate( long new_rate, int msec )
{
	// start with maximum length that resampled time can represent
	blip_s64 new_size = (ULLONG_MAX >> BLIP_BUFFER_ACCURACY) - blip_buffer_extra_ - 64;

	// simple safety check, since code elsewhere may not be safe for sizes approaching (2 ^ 31).
	if(new_size > ((1LL << 30) - 1))
	 new_size = (1LL << 30) - 1;

	if ( msec != blip_max_length )
	{
		blip_s64 s = ((blip_s64)new_rate * (msec + 1) + 999) / 1000;
		if ( s < new_size )
			new_size = s;
		else
			assert( 0 ); // fails if requested buffer length exceeds limit
	}
	
	if ( buffer_size_ != new_size )
	{
		void* p = realloc( buffer_, (new_size + blip_buffer_extra_) * sizeof *buffer_ );
		if ( !p )
			return "Out of memory";

		buffer_ = (buf_t_*) p;
	}
	
	buffer_size_ = new_size;
	
	// update things based on the sample rate
	sample_rate_ = new_rate;
	length_ = new_size * 1000 / new_rate - 1;
	if ( msec )
		assert( length_ == msec ); // ensure length is same as that passed in
	if ( clock_rate_ )
		clock_rate( clock_rate_ );
	bass_freq( bass_freq_ );
	
	clear();
	
	return 0; // success
}

blip_resampled_time_t Blip_Buffer::clock_rate_factor( long rate ) const
{
	double ratio = (double) sample_rate_ / rate;
	blip_s64 factor = (blip_s64) floor( ratio * (1LL << BLIP_BUFFER_ACCURACY) + 0.5 );
	assert( factor > 0 || !sample_rate_ ); // fails if clock/output ratio is too large
	return (blip_resampled_time_t) factor;
}

void Blip_Buffer::bass_freq( int freq )
{
	bass_freq_ = freq;
	int shift = 31;
	if ( freq > 0 )
	{
		shift = 13;
		long f = (freq << 16) / sample_rate_;
		while ( (f >>= 1) && --shift ) { }
	}
	bass_shift_ = shift;
}

void Blip_Buffer::end_frame( blip_time_t t )
{
	offset_ += t * factor_;
	assert( samples_avail() <= (long) buffer_size_ ); // time outside buffer length
}

void Blip_Buffer::remove_silence( long count )
{
	assert( count <= samples_avail() ); // tried to remove more samples than available
	offset_ -= (blip_resampled_time_t) count << BLIP_BUFFER_ACCURACY;
}

long Blip_Buffer::count_samples( blip_time_t t ) const
{
	unsigned long last_sample  = resampled_time( t ) >> BLIP_BUFFER_ACCURACY;
	unsigned long first_sample = offset_ >> BLIP_BUFFER_ACCURACY;
	return (long) (last_sample - first_sample);
}

blip_time_t Blip_Buffer::count_clocks( long count ) const
{
	if ( !factor_ )
	{
		assert( 0 ); // sample rate and clock rates must be set first
		return 0;
	}
	
	if ( count > buffer_size_ )
		count = buffer_size_;
	blip_resampled_time_t time = (blip_resampled_time_t) count << BLIP_BUFFER_ACCURACY;
	return (blip_time_t) ((time - offset_ + factor_ - 1) / factor_);
}

void Blip_Buffer::remove_samples( long count )
{
	if ( count )
	{
		remove_silence( count );
		
		// copy remaining samples to beginning and clear old samples
		long remain = samples_avail() + blip_buffer_extra_;
		memmove( buffer_, buffer_ + count, remain * sizeof *buffer_ );
		memset( buffer_ + remain, 0, count * sizeof *buffer_ );
	}
}

// Blip_Synth_

Blip_Synth_Fast_::Blip_Synth_Fast_()
{
	buf = 0;
	last_amp = 0;
	delta_factor = 0;
}

void Blip_Synth_Fast_::volume_unit( double new_unit )
{
	delta_factor = int (new_unit * (1L << blip_sample_bits) + 0.5);
}

long Blip_Buffer::read_samples( blip_sample_t* BLIP_RESTRICT out, long max_samples, int stereo )
{
	long count = samples_avail();
	if ( count > max_samples )
		count = max_samples;
	
	if ( count )
	{
		int const bass = BLIP_READER_BASS( *this );
		BLIP_READER_BEGIN( reader, *this );
		
#ifndef WANT_STEREO_SOUND
		if ( !stereo )
		{
			for ( blip_long n = count; n; --n )
			{
				blip_long s = BLIP_READER_READ( reader );
				if ( (blip_sample_t) s != s )
					s = 0x7FFF - (s >> 24);
				*out++ = (blip_sample_t) s;
				BLIP_READER_NEXT( reader, bass );
			}
		}
		else
#endif
		{
			for ( blip_long n = count; n; --n )
			{
				blip_long s = BLIP_READER_READ( reader );
				if ( (blip_sample_t) s != s )
					s = 0x7FFF - (s >> 24);
				*out = (blip_sample_t) s;
				out += 2;
				BLIP_READER_NEXT( reader, bass );
			}
		}
		BLIP_READER_END( reader, *this );
		
		remove_samples( count );
	}
	return count;
}

void Blip_Buffer::mix_samples( blip_sample_t const* in, long count )
{
	buf_t_* out = buffer_ + (offset_ >> BLIP_BUFFER_ACCURACY) + blip_widest_impulse_ / 2;
	
	int const sample_shift = blip_sample_bits - 16;
	int prev = 0;
	while ( count-- )
	{
		blip_long s = (blip_long) *in++ << sample_shift;
		*out += s - prev;
		prev = s;
		++out;
	}
	*out -= prev;
}


// Copyright Epic Games, Inc. All Rights Reserved.
// This source file is licensed solely to users who have
// accepted a valid Unreal Engine license agreement 
// (see e.g., https://www.unrealengine.com/eula), and use
// of this source file is governed by such agreement.

// @cdep pre $cbtargetsse4

#include "rrsurfacefilters.h"
#include "rrsurfaceutil.h"
#include "rrcolor.h"
#include "rrsurfaceblit.h"
#include "rrsurfacerowcache.h"
#include "rrvecc.h"
#include <string.h>
#include "rrlogutil.h"
#include "templates/rrstl.h"
#include "templates/rrvector_st.h"
#include "threadprofiler.h"

#include "newlz_simd.h"
#ifdef DO_BUILD_SSE4
#include "vec128.inl"
#else
#define VecF32x4 float
#endif

RR_NAMESPACE_START

//====================================================

#define TINY_FILTER_WEIGHT_I_DONT_CARE	(1.0 / 256.0)
#define MAX_FILTER_TAPS					64

template <typename t_color>
static RADFORCEINLINE void rrSurface_MakeGaussianBlurred_Sub_H_Line_Border(t_color * to_row,const t_color * from_row,
											int start,int end, int w,
											const F32 * filter,int filter_width)
{
	for(int x=start;x<end;x++) 
	{
		t_color accum;
		accum = filter[0] * from_row[x];
		for (int i=1;i<filter_width;i++) // no i=0
			accum += filter[i] * (from_row[ MirrorIndex(x+i,w) ] + from_row[ MirrorIndex(x-i,w) ]);
			
		to_row[x] = accum;
	}
}

static RADINLINE void rrSurface_MakeGaussianBlurred_Sub_Line_Interior(float *to_ptr,const float * fm_ptr,
	SINTa stride, // stride is in floats
	SINTa count,
	const VecF32x4 * vfilter,
	const F32 * filter,int filter_width)
{
	#ifdef __RADSSE2__

	SINTa count4 = count/8;
	for LOOP(c,count4)
	{
		VecF32x4 accum0 = vfilter[0] * VecF32x4::loadu(fm_ptr);
		VecF32x4 accum1 = vfilter[0] * VecF32x4::loadu(fm_ptr+4);
		for (int i=1;i<filter_width;i++) // no i=0
		{
			accum0 += vfilter[i] * (VecF32x4::loadu(fm_ptr + i*stride) + VecF32x4::loadu(fm_ptr - i*stride));
			accum1 += vfilter[i] * (VecF32x4::loadu(fm_ptr + i*stride + 4) + VecF32x4::loadu(fm_ptr - i*stride + 4));
		}
		
		accum0.storeu(to_ptr);
		accum1.storeu(to_ptr+4);
		to_ptr += 8;
		fm_ptr += 8;
	}
	count4 *= 8;

	#else

	SINTa count4=0;

	#endif

	for(SINTa c=count4;c<count;c++)
	{
		F32 accum = filter[0] * fm_ptr[0];
		for (int i=1;i<filter_width;i++) // no i=0
			accum += filter[i] * (fm_ptr[ i*stride ] + fm_ptr[ -i*stride ]);
			
		*to_ptr++ = accum;
		fm_ptr++;
	}
}

#define STRIP_WIDTH_FLOATS	384	// should have a factor of 3 in it for nchan=3
// STRIP_WIDTH_FLOATS determines hot cache working set; you want it to fit in L1

static void rrSurface_MakeGaussianBlurred_Sub_Strip_H(
	float * to_row,
	const float * from_row,
	int nchan,
	int stripx,int stripw,int w,
	bool is_borderx,
	const VecF32x4 *vfilter, const F32 * filter,int filter_width)
{
	// from_row should be indexed by [x] in stripx/stripw
	// to_row should be indexed in [0-stripw]

	if ( is_borderx )
	{
		// @@ not happy with this branch
		//	does the whole strip in "border" mode
		//	only needs to do some pixels
		// with STRIP_WIDTH == 64 this is non-trivial waste
		//
		// (but testing said it's faster than trying to vectorize more of it)
		float * to_row_start = to_row - stripx*nchan;
		switch(nchan)
		{
		case 1: rrSurface_MakeGaussianBlurred_Sub_H_Line_Border<float>(to_row_start,from_row,stripx,stripx+stripw,w,filter,filter_width); break;
		case 2: rrSurface_MakeGaussianBlurred_Sub_H_Line_Border<rrVec2f>((rrVec2f*)to_row_start,(const rrVec2f*)from_row,stripx,stripx+stripw,w,filter,filter_width); break;
		case 3: rrSurface_MakeGaussianBlurred_Sub_H_Line_Border<rrVec3f>((rrVec3f*)to_row_start,(const rrVec3f*)from_row,stripx,stripx+stripw,w,filter,filter_width); break;
		case 4: rrSurface_MakeGaussianBlurred_Sub_H_Line_Border<rrVec4f>((rrVec4f*)to_row_start,(const rrVec4f*)from_row,stripx,stripx+stripw,w,filter,filter_width); break;
		RR_NO_DEFAULT_CASE
		}
	}
	else
	{
		rrSurface_MakeGaussianBlurred_Sub_Line_Interior(
			to_row,
			from_row+stripx*nchan,
			nchan,
			stripw*nchan,
			vfilter,filter,filter_width);
	}
}

// count is in _floats_ not pixels
static void rrSurface_MakeGaussianBlurred_Sub_Strip_V(
	float * to_row,
	F32 * * window_rows,
	int count,
	const VecF32x4 * vfilter, const F32 * filter,int filter_width)
{
	int floati = 0;
	#ifdef __RADSSE2__
	SINTa count4 = count&(~7);
	for(;floati<count4;floati+=8)
	{
		const F32 * row0 = window_rows[0] + floati;
		VecF32x4 accum0 = vfilter[0] * VecF32x4::loadu(row0);
		VecF32x4 accum1 = vfilter[0] * VecF32x4::loadu(row0+4);
		for (int i=1;i<filter_width;i++) // no i=0
		{
			const F32 * rowpi = window_rows[i] + floati;
			const F32 * rowni = window_rows[-i] + floati;
			accum0 += vfilter[i] * (VecF32x4::loadu(rowpi  ) + VecF32x4::loadu(rowni  ));
			accum1 += vfilter[i] * (VecF32x4::loadu(rowpi+4) + VecF32x4::loadu(rowni+4));
		}

		accum0.storeu(to_row+floati);
		accum1.storeu(to_row+floati+4);
	}
	#endif

	// fm_rows you can always read aligned, so we could do a final vector
	// but then the final store *can't* over-write
	//	you could just do one more vector and then do a partial store

	for(;floati<count;floati++)
	{
		float accum = filter[0] * window_rows[0][floati];
		for (int i=1;i<filter_width;i++) // no i=0
			accum += filter[i] * (window_rows[i][floati] + window_rows[-i][floati]);
		
		to_row[floati] = accum;
	}
}

void rrSurface_MakeGaussianBlurred_Sub_Strip(rrSurface * pTo,const rrSurface & from_F32,
	rrPixelConsumerFunc * consumer, void * consumer_ctx,
	int nchan,int stripx,int stripw,
	F32 * * window_rows,int window_height,
	const VecF32x4 * vfilter, const F32 * filter,int filter_width)
{
	int w = from_F32.width;
	int h = from_F32.height;
	int filter_extent = filter_width - 1; // filter, after mirroring, goes [-filter_extent,filter_extent]
	int stripc = stripw * nchan;

	bool is_borderx = stripx <= filter_width || (stripx+stripw+filter_width) >= from_F32.width;
	int cur_window_y = 0; // cur_window_y tracks where current y maps to in the circular window

	// y here ranges over rows we have H filtered; V filtering lags behind which is why we
	// do some extra iterations
	for LOOP(y,h+filter_extent)
	{
		// keep H blurring new rows until we run out of image
		if ( y < h )
		{
			rrSurface_MakeGaussianBlurred_Sub_Strip_H(
				window_rows[cur_window_y],
				(const float *)(from_F32.data + y*from_F32.stride),
				nchan,stripx,stripw,w,is_borderx,
				vfilter,filter,filter_width);
		}
		else
		{
			// after we run out of image, copy over mirrored rows
			// the rows we're reflecting to are guaranteed to be in the window
			memcpy(
				window_rows[cur_window_y],
				window_rows[cur_window_y + (MirrorIndex(y,h) - y)],
				stripc * sizeof(float)
			);
		}

		// once we have enough input rows buffered, we can start producing output
		int dest_y = y - filter_extent;

		// first row takes care of mirroring the initial rows into the window
		// the rows we're reflecting to are guaranteed to be in the window
		if ( dest_y == 0 )
		{
			for(int j = -filter_extent;j < 0;j++)
			{
				memcpy(
					window_rows[j],
					window_rows[MirrorIndex(j,h)],
					stripc * sizeof(float)
				);
			}
		}

		if ( dest_y >= 0 )
		{
			float output_buf[STRIP_WIDTH_FLOATS];
			float * to_ptr = output_buf; // can always write to temp buf on stack

			if ( pTo )
				to_ptr = (float *)(pTo->data + stripx*nchan*sizeof(float) + dest_y*pTo->stride);

			rrSurface_MakeGaussianBlurred_Sub_Strip_V(
				to_ptr,
				window_rows + cur_window_y - filter_extent,
				stripc,
				vfilter,filter,filter_width);

			if ( consumer )
				consumer(consumer_ctx, to_ptr, from_F32.pixelFormat, stripx,dest_y,stripw);
		}

		// increment current window y with wraparound
		if ( ++cur_window_y == window_height )
			cur_window_y = 0;
	}
}

static void rrSurface_MakeGaussianBlurred_Sub_Strips(rrSurface * pTo,const rrSurface & from_F32,
	rrPixelConsumerFunc * consumer, void * consumer_ctx,
	int nchan,
	const F32 * filter,int filter_width)
{
	THREADPROFILESCOPE("MakeGaussianBlurred_Sub");

	#ifdef RR_DO_ASSERTS
	{
	RR_ASSERT( nchan >= 1 && nchan <= 4 );
	rrPixelFormat pf = RR_MAKE_PIXELFORMAT_SIMPLE(nchan,RR_PIXELFORMAT_TYPE_F32);
	RR_ASSERT( pf == from_F32.pixelFormat );
	RR_ASSERT( ! pTo || pf == pTo->pixelFormat );
//	RR_ASSERT( rrPixelFormat_GetInfo(pf)->bytesPerPixel == sizeof(t_color) );
	}
	#endif
	
	// we keep a circular window of rows just large enough to keep all the data we need to
	// output a single row; determine how many rows that is and set up our row pointers
	int filter_extent = filter_width - 1; // filter, after mirroring, goes [-filter_extent,filter_extent]
	int window_height = 2*filter_extent + 1;

	SINTa window_nelems = window_height * STRIP_WIDTH_FLOATS;
	F32 * window_buf = OODLE_MALLOC_ARRAY_ALIGNED(F32,window_nelems,64);

	// set up an array of row pointers with extra row pointers above and below
	// that have the wraparound already applied, so our indexing in the hot loops
	// is trivial
	//
	// output loop lags by filter_extent rows and accesses rows with indices in
	// [-filter_extent,filter_extent], so we need an extra 2*filter_extent
	// row pointers for the negative indices
	vector_st<F32 *, 32> window_rows_vec;
	window_rows_vec.resize(2*filter_extent + window_height);

	F32 * * window_rows = &window_rows_vec[2*filter_extent]; // make row 0 be at index 0
	for (int y = -2*filter_extent; y < 0; y++)
		window_rows[y] = &window_buf[(y + window_height) * STRIP_WIDTH_FLOATS];
	for (int y = 0; y < window_height; y++)
		window_rows[y] = &window_buf[y * STRIP_WIDTH_FLOATS];

	VecF32x4 vfilter[MAX_FILTER_TAPS];
	for(int i=0;i<filter_width;i++)
		vfilter[i] = VecF32x4(filter[i]);

	int w = from_F32.width;
	int stripw = STRIP_WIDTH_FLOATS / nchan;
	
	for(int sx=0;sx < w;sx += stripw)
	{
		int sxend = RR_MIN(w,sx+stripw);
		int sw = sxend - sx;

		rrSurface_MakeGaussianBlurred_Sub_Strip(pTo,from_F32,consumer,consumer_ctx,nchan,sx,sw,
			window_rows,window_height,
			vfilter,filter,filter_width);
	}

	OODLE_FREE_ARRAY(window_buf,window_nelems);
}

//================================================

static int make_gaussian(F32 filter[MAX_FILTER_TAPS], double sigma)
{
	// filter is centered on filter[0]
	//	only one half is stored, symmetric mirrored

	// Integral of exp(-1/2 (x / sigma)^2) = sigma * sqrt(2pi)
	// that's gonna be approximately our final normalization sum, so use that
	// to figure out where to place our cut-off
	double cutoff = TINY_FILTER_WEIGHT_I_DONT_CARE * sigma * 2.506628274631;

	// make filter taps,
	//	stop when they go below 0.01
	double sum = 0;
	int filter_width = 1; // filter[0] is tap0 
	filter[0] = 1.0f;
	while( filter_width < MAX_FILTER_TAPS )
	{
		double val = rr_exp_approx( - 0.5 * fsquare(filter_width / sigma) );
		if ( val < cutoff )
			break;

		sum += val;
		filter[filter_width] = (F32)val;
		filter_width++;
	}

	if ( filter_width == MAX_FILTER_TAPS )
	{
		rrprintf("WARNING : filter_width == MAX_FILTER_TAPS\n");
		rrprintfvar(filter[filter_width-1]);
	}

	sum *= 2; // two sides
	sum += 1.0; // the tap at zero
	
	F32 tap0 = (F32)(1.0/sum);
	// normalize :
	for LOOP(i,filter_width)
		filter[i] *= tap0;

	RR_ASSERT( filter[0] == tap0 );
	
	// print last value :
	//rrprintfvar(filter_width);
	//rrprintfvar(filter[filter_width-1]);

	/**
	
	// 07-10-2020 :

	lowpass_sigma = 2.2 gives these :

	could definitely lose that last value

	filter_width	7	int
-		filter,7	0x000000f7f559ea10 {0.181861535, 0.164012045, 0.120304070, 0.0717719421, 0.0348256119, 0.0137439827, ...}	float[7]
		[0]	0.181861535	float
		[1]	0.164012045	float
		[2]	0.120304070	float
		[3]	0.0717719421	float
		[4]	0.0348256119	float
		[5]	0.0137439827	float
		[6]	0.00441160053	float


	**/

	return filter_width;
}

rrbool rrSurface_MakeGaussianBlurred(rrSurface * pTo,const rrSurface & from,double sigma)
{
	F32 filter[MAX_FILTER_TAPS];
	int filter_width = make_gaussian(filter,sigma);

	int nc = rrPixelFormat_GetInfo(from.pixelFormat)->channels;
	rrPixelFormat pf = RR_MAKE_PIXELFORMAT_SIMPLE(nc,RR_PIXELFORMAT_TYPE_F32);

	rrSurfaceObj from_F32;
	if ( pf == from.pixelFormat )
		rrSurface_SetView(&from_F32,&from);
	else if ( ! rrSurface_AllocCopy_ChangeFormatNonNormalized(&from_F32,&from,pf) )
		return false;

	// NOTE(fg): need to go to temp destination because we allow from==to.
	// we swap this into the actual pTo at the end
	rrSurfaceObj to;
	int w = from.width;
	int h = from.height;
	if ( ! rrSurface_Alloc(&to,w,h,pf) )
		return false;

	rrSurface_MakeGaussianBlurred_Sub_Strips(&to,from_F32,0,0,nc,filter,filter_width);
	rrSurface_Swap(pTo,&to);

	return true;
}

rrbool rrSurface_MakeGaussianBlurredWithConsumer(const rrSurface & from,double sigma,rrPixelConsumerFunc * consumer,void * consumer_ctx)
{
	F32 filter[MAX_FILTER_TAPS];
	int filter_width = make_gaussian(filter,sigma);

	int nc = rrPixelFormat_GetInfo(from.pixelFormat)->channels;
	rrPixelFormat pf = RR_MAKE_PIXELFORMAT_SIMPLE(nc,RR_PIXELFORMAT_TYPE_F32);
	if ( from.pixelFormat != pf )
		return false;

	rrSurface_MakeGaussianBlurred_Sub_Strips(0,from,consumer,consumer_ctx,nc,filter,filter_width);
	return true;
}

template <typename t_color>
void rrSurface_MakeBilateralFiltered_Sub(rrSurface * pTo,const rrSurface & from_F32,const rrSurface & center_F32,int half_width, F32 inv_sigmasqr_spatial, F32 inv_sigmasqr_value)
{
	int w = from_F32.width;
	int h = from_F32.height;
	
	#ifdef RR_DO_ASSERTS
	{
	int nc = sizeof(t_color)/sizeof(F32);
	rrPixelFormat pf = RR_MAKE_PIXELFORMAT_SIMPLE(nc,RR_PIXELFORMAT_TYPE_F32);
	RR_ASSERT( pf == from_F32.pixelFormat );
	RR_ASSERT( pf == center_F32.pixelFormat );
	RR_ASSERT( pf == pTo->pixelFormat );
	RR_ASSERT( rrPixelFormat_GetInfo(pf)->bytesPerPixel == sizeof(t_color) );
	}
	#endif
	
	// filter rows : (from->to)
	for LOOP(y,h)
	{
		t_color * to_row   = (t_color *) rrSurface_Seek(pTo,0,y);
		//const t_color * from_row_center = (const t_color *) rrSurface_SeekC(&from_F32,0,y);
		const t_color * from_row_center = (const t_color *) rrSurface_SeekC(&center_F32,0,y);
		
		for LOOP(x,w)
		{
			t_color center = from_row_center[x];
			
			// center sample :
			//t_color accum = center;
			//F32 total_weight = 1.0;

			t_color accum;
			SetZero(&accum);
			F32 total_weight = 0.f;

			for (int dy=-half_width;dy<=half_width;dy++)
			{
				const t_color * from_row_cur = (const t_color *) rrSurface_SeekC(&from_F32,0, MirrorIndex(y+dy,h));
		
				for (int dx=-half_width;dx<=half_width;dx++)
				{
					int dsqr_spatial = dx*dx + dy*dy;
					// no center is not done
					//if ( dsqr_spatial == 0 ) continue; // center already done
					if ( dsqr_spatial > half_width*half_width ) continue; // tiny weight
					
					t_color cur_value = from_row_cur[ MirrorIndex(x+dx,w) ];
					F32 dsqr_value = (F32) DeltaSqr( center, cur_value );
					
					F32 t = inv_sigmasqr_spatial * dsqr_spatial + inv_sigmasqr_value * dsqr_value;
					if ( t > 10.f ) continue; // meh tiny
					//if ( t > 20.f ) continue; // avoid denorms
					F32 weight = rr_expf_approx( - t );
					//if ( w < FLT_MIN ) continue; // avoid denorms
					//cur_value *= w;
					accum += weight * cur_value;
					total_weight += weight;
				}
			}
			
			to_row[x] = accum * (1.f / total_weight);
		}
	}
	
}

rrbool rrSurface_MakeBilateralFiltered(rrSurface * pTo_can_be_from,const rrSurface & from,const rrSurface & center,double sigma_spatial, double sigma_value)
{
	// from == center is standard

	F32 inv_sigmasqr_spatial = (F32)( 0.5 / fsquare(sigma_spatial) );
	F32 inv_sigmasqr_value = (F32)( 0.5 / fsquare(sigma_value) );
	
	// automatic half_width :
	int half_width = 3;
	for(;;)
	{
		F32 t = inv_sigmasqr_spatial * half_width*half_width;
		F32 w = rr_expf_approx( - t );
		if ( w < TINY_FILTER_WEIGHT_I_DONT_CARE )
		{
			half_width--;
			break;
		}
		half_width++;
	}
	
	int nc = rrPixelFormat_GetInfo(from.pixelFormat)->channels;
	rrPixelFormat pf = RR_MAKE_PIXELFORMAT_SIMPLE(nc,RR_PIXELFORMAT_TYPE_F32);

	// make a copy to get into an F32 standard format
	//	rrSurface_MakeBilateralFiltered_Sub mutates from so we always need a copy anyway
	//	copying here also makes fm==to okay
	rrSurfaceObj from_F32;
	if ( ! rrSurface_AllocCopyOrSetViewIfFormatMatches_NonNormalized(&from_F32,&from,pf) )
		return false;
			
	rrSurfaceObj center_F32;
	if ( ! rrSurface_AllocCopyOrSetViewIfFormatMatches_NonNormalized(&center_F32,&center,pf) )
		return false;
	
	// alloc to "dest"
	//	this allows pTo == &source
	rrSurfaceObj dest;
	int w = from.width;
	int h = from.height;
	if ( ! rrSurface_Alloc(&dest,w,h,pf) )
		return false;
	
	switch(pf)
	{
	case rrPixelFormat_1_F32:
		rrSurface_MakeBilateralFiltered_Sub<F32>(&dest,from_F32,center_F32,half_width,inv_sigmasqr_spatial,inv_sigmasqr_value);
		break;
	case rrPixelFormat_2_F32:
		rrSurface_MakeBilateralFiltered_Sub<rrVec2f>(&dest,from_F32,center_F32,half_width,inv_sigmasqr_spatial,inv_sigmasqr_value);
		break;
	case rrPixelFormat_3_F32:
		rrSurface_MakeBilateralFiltered_Sub<rrVec3f>(&dest,from_F32,center_F32,half_width,inv_sigmasqr_spatial,inv_sigmasqr_value);
		break;
	case rrPixelFormat_4_F32:
		rrSurface_MakeBilateralFiltered_Sub<rrVec4f>(&dest,from_F32,center_F32,half_width,inv_sigmasqr_spatial,inv_sigmasqr_value);
		break;
	RR_NO_DEFAULT_CASE
	}
		
	rrSurface_Swap(&dest,pTo_can_be_from);
	
	return true;
}


RR_NAMESPACE_END

/*! \file	essence.h
 *	\brief	Definition of classes that handle essence reading and writing
 *
 *	\version $Id$
 *
 */
/*
 *  This software is provided 'as-is', without any express or implied warranty.
 *  In no event will the authors be held liable for any damages arising from
 *  the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must
 *      not claim that you wrote the original software. If you use this
 *      software in a product, you must include an acknowledgment of the
 *      authorship in the product documentation.
 *
 *   2. Altered source versions must be plainly marked as such, and must
 *      not be misrepresented as being the original software.
 *
 *   3. This notice may not be removed or altered from any source
 *      distribution.
 */
#ifndef MXFLIB__ESSENCE_H
#define MXFLIB__ESSENCE_H


#include <map>
#include <list>
#include <vector>


// Forward refs
namespace mxflib
{
	//! Smart pointer to a GCWriter
	class GCWriter;
	typedef SmartPtr<GCWriter> GCWriterPtr;

	class GCReader;
	typedef SmartPtr<GCReader> GCReaderPtr;

	// Type used to identify stream
	typedef int GCStreamID;

	// Forward declare
	class BodyStream;

	//! Smart pointer to a BodyStream
	typedef SmartPtr<BodyStream> BodyStreamPtr;

	//! Parent pointer to a BodyStream
	typedef ParentPtr<BodyStream> BodyStreamParent;

	//! List of smart pointers to BodyStreams
	typedef std::list<BodyStreamPtr> BodyStreamList;

	// Forward declare
	class EssenceSubParser;

	//! Smart pointer to an EssenceSubParser
	typedef SmartPtr<EssenceSubParser> EssenceSubParserPtr;

	//! PArent pointer to an EssenceSubParser
	typedef ParentPtr<EssenceSubParser> EssenceSubParserParent;
}


/* Global definitions */
namespace mxflib
{
	//! Flag that allows faster clip wrapping using random access
	/*! Clip wrapped essence may contain huge essence KLVs and it is often not
	 *  practical (or even possible) to load the whole value into memory before
	 *  writing the K and L. This means that unless it is possible to use some
	 *  shortcut to calculate the size of the value before building it, the value
	 *  will need to be 'built' twice - once without storing the data to enable
	 *  its length to be calculated, then again to actually write it.
	 *  "FastClipWrap" mode gets around this by writing the length as (2^56)-1,
	 *  the largest 8-byte BER length, writing the value, then returning to update
	 *  the length field with the correct size. This huge length ensures that any
	 *  reader that is attempting to read the file while it is being written will
	 *  have a lower chance of barfing than if any "guestimate" value is written.
	 *  The reader will see the whole of the rest of the file as the essence.
	 *  This method requires random access to the medium holding the MXF file
	 *  being written, therefore is disable by default.
	 */
	extern bool AllowFastClipWrap;

	//! Enable or disable "FastClipWrap" mode
	inline void SetFastClipWrap(bool Flag) { AllowFastClipWrap = Flag; }

	//! Read the status of the "FastClipWrap" mode flag
	inline bool GetFastClipWrap(void) { return AllowFastClipWrap; }
}


namespace mxflib
{
	//! Wrapping options for an EssenceSubParser or an EssenceSubStream
	class WrappingOption : public RefCount<WrappingOption>
	{
	public:
		//! Wrapping type
		/*! \note "None" is only for use as a default condition */
		enum WrapType { None, Frame, Clip, Line, Other } ;
		EssenceSubParserParent Handler;			//!< Pointer to the object that can parse this wrapping option - parent pointer because the parser holds a copy of this!
		std::string Name;						//!< A short name, unique for this sub-parser, for this wrapping option (or "" if not supported by this handler)
		std::string Description;				//!< Human readable description of this wrapping option (to allow user selection)
		ULPtr	WrappingID;						//!< A UL (or endian swapped UUID) that uniquely identifies this sub-parser/wrapping option combination (or NULL if not suppoered by this handler)
												/*!< This allows an application to specify a desired wrapping, or list of wrappings, for automated selection */
		ULPtr	WrappingUL;						//!< UL for this wrapping
		ULList	RequiredPartners;				//!< List of other items that *MUST* accompany this item to use this wrapping
		UInt8	GCEssenceType;					//!< The Generic Container essence type, or 0 if not a GC wrapping
		UInt8	GCElementType;					//!< The Generic Container element value, or 0 if not a GC wrapping
		WrapType ThisWrapType;					//!< The type of this wrapping (frame, clip etc.)
		bool	CanSlave;						//!< True if this wrapping can be a "slave" which allows it to be used at a different edit rate than its own
		bool	CanIndex;						//!< True if this wrapping can be VBR indexed by the handler (CBR essence may need VBR indexing when interleaved)
		bool	CBRIndex;						//!< True if this wrapping may use a CBR index table (and therefore have a non-zero return value from GetBytesPerEditUnit() )
		UInt8	BERSize;						//!< The BER length size to use for this wrapping (or 0 for any)
		UInt32 BytesPerEditUnit;				//!< set non zero for ConstSamples
	};

	typedef SmartPtr<WrappingOption> WrappingOptionPtr;
	typedef std::list<WrappingOptionPtr> WrappingOptionList;


	//! Abstract super-class for objects that supply large quantities of essence data
	/*! This is used when clip-wrapping to prevent large quantities of data being loaded into memory
	 *! \note Classes derived from this class <b>must not</b> include their own RefCount<> derivation
	 */
	class EssenceSource : public RefCount<EssenceSource>
	{
	protected:
		//! Holds the stream ID for this essence stream when added to a GCWriter
		/*! This value is persisted here between calls to a GCWriter via BodyWriter or similar.
		 *  Set to -1 if no stream ID yet set.
		 */
		GCStreamID StreamID;

		//! Index manager to use if we can index the essence
		IndexManagerPtr IndexMan;

		//! Sub-stream ID to use for our index data if we can index the essence
		int IndexStreamID;

		//! If the default essence key has been overridden for this source it is stored here
		DataChunkPtr SpecifiedKey;

		//! True if the default essence key has been overridden with a key that does not use GC track number mechanism
		bool NonGC;

		//! Number of frames that should be sent, used to match lengths of streams where appropriate, or -1 for undefined
		Length LenToSend;

		//! The essence descriptor describing this essence (if known) else NULL
		/*! The essence descriptor will be set by the wrapping application once the source has been selected and configured.
		 *  This means that the essence source has access to the full metadata that will be written into the file in case iit is required.
		 *  /note This descriptor may be the same one supplied by EssenceSubParser::IdentifyEssence() or a modified copy of it, or a completely new object
		 */
		MDObjectPtr EssenceDescriptor;

		//! If we are held in a BodyStream, a parent pointer to it is held here
		BodyStreamParent BodyParent;


	public:
		//! Base constructor
		EssenceSource() : StreamID(-1), LenToSend(-1)
		{
		};

		//! Virtual destructor to allow polymorphism
		virtual ~EssenceSource() { };

		//! Get the size of the next "installment" of essence data in bytes
		/*! \note There is intentionally no support for an "unknown" response */
		virtual size_t GetEssenceDataSize(void) = 0;

		//! Get the next "installment" of essence data
		/*! This will attempt to return an entire wrapping unit (e.g. a full frame for frame-wrapping) but will return it in
		 *  smaller chunks if this would break the MaxSize limit. If a Size is specified then the chunk returned will end at
		 *  the first wrapping unit end encountered before Size. On no account will portions of two or more different wrapping
		 *  units be returned together. The mechanism for selecting a type of wrapping (e.g. frame, line or clip) is not
		 *  (currently) part of the common EssenceSource interface.
		 *  \return Pointer to a data chunk holding the next data or a NULL pointer when no more remains
		 *	\note If there is more data to come but it is not currently available the return value will be a pointer to an empty data chunk
		 *	\note If Size = 0 the object will decide the size of the chunk to return
		 *	\note On no account will the returned chunk be larger than MaxSize (if MaxSize > 0)
		 */
		virtual DataChunkPtr GetEssenceData(size_t Size = 0, size_t MaxSize = 0) = 0;

		//! Did the last call to GetEssenceData() return the end of a wrapping item
		/*! \return true if the last call to GetEssenceData() returned an entire wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the last chunk of a wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the end of a clip-wrapped clip.
		 *  \return false if there is more data pending for the current wrapping unit.
		 *  \return false if the source is to be clip-wrapped and there is more data pending for the clip
		 */
		virtual bool EndOfItem(void) = 0;

		//! Is all data exhasted?
		/*! \return false if a call to GetEssenceData() will return some valid essence data
		 */
		virtual bool EndOfData(void) = 0;

		//! Get data to write as padding after all real essence data has been processed
		/*! If more than one stream is being wrapped, they may not all end at the same wrapping-unit.
		 *	When this happens each source that has ended will produce NULL is response to GetEssenceData().
		 *	The default action of the caller would be to write zero-length KLVs in each wrapping unit for each source that has ended.
		 *	If a source supplies an overload for this method, the supplied padding data will be written in wrapping units following the end of essence instead of a zero-length KLV
		 *	DRAGONS: Note that as the returned value is a non-smart pointer, ownership of the buffer stays with the EssenceSource object.
		 *			 The recommended method of operation is to have a member DataChunk (or DataChunkPtr) allocated the first time padding is required, and return the address each call.
		 *			 The destructor must then free the DataChunk, or allow the smart DataChunkPtr to do it automatically
		 */
		virtual DataChunk *GetPadding(void) { return NULL; }

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCEssenceType(void) = 0;

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCElementType(void) = 0;

		//! Set the stream ID for this stream or sub-stream
		void SetStreamID(GCStreamID NewID) { StreamID = NewID; }

		//! Get the stream ID for this stream or sub-stream
		GCStreamID GetStreamID(void) { return StreamID; }

		//! Is the last data read the start of an edit point?
		virtual bool IsEditPoint(void) { return true; }

		//! Get the edit rate of this wrapping of the essence
		/*! \note This may not be the same as the original "native" edit rate of the
		 *        essence if this EssenceSource is wrapping to a different edit rate
		 */
		virtual Rational GetEditRate(void) = 0;

		//! Get the current position in GetEditRate() sized edit units
		/*! This is relative to the start of the stream, so the first edit unit is always 0.
		 *  This is the same as the number of edit units read so far, so when the essence is
		 *  exhausted the value returned shall be the size of the essence
		 */
		virtual Position GetCurrentPosition(void) = 0;

		//! Get the preferred BER length size for essence KLVs written from this source, 0 for auto
		virtual int GetBERSize(void) { return 0; }

		//! Set a wrapping option for future Read and Write calls
		/*! \return true if this wrapping option is suitable for use, else false
		 */
		virtual bool Use(WrappingOptionPtr &UseWrapping)
		{
			return true;
		}

		//! Set a non-native edit rate
		/*! \return true if this rate is acceptable */
		virtual bool SetEditRate(Rational EditRate)
		{
			// Default action is to not allow the edit rate to be changed
			return (EditRate == GetEditRate());
		}

		//! Set a source type or parser specific option
		/*! \return true if the option was successfully set */
		virtual bool SetOption(std::string Option, Int64 Param = 0) { return false; } ;

		//! Get BytesPerEditUnit if Constant, else 0
		/*! \note This value may be useful even if CanIndex() returns false
		 */
		virtual UInt32 GetBytesPerEditUnit(UInt32 KAGSize = 1) { return 0; }

		//! Can this stream provide indexing
		/*! If true then SetIndex Manager can be used to set the index manager that will receive indexing data
		 */
		virtual bool CanIndex() { return false; }

		//! Set the index manager to use for building index tables for this essence
		/*! \note The values are stored even if this stream does not support indexing as a derived stream may do
		 */
		virtual void SetIndexManager(IndexManagerPtr &Manager, int StreamID)
		{
			IndexMan = Manager;
			IndexStreamID = StreamID;
		}

		//! Get the index manager
		virtual IndexManagerPtr &GetIndexManager(void) { return IndexMan; }

		//! Get the index manager sub-stream ID
		virtual int GetIndexStreamID(void) { return IndexStreamID; }

		//! Override the default essence key
		virtual void SetKey(DataChunkPtr &Key, bool NonGC = false)
		{
			mxflib_assert(Key->Size == 16);

			SpecifiedKey = Key;
			this->NonGC = NonGC;
		}

		//! Get the current overridden essence key
		/*! DRAGONS: If the key has not been overridden NULL will be returned - not the default key
		 *  \note Defined EssenceSource sub-classes may always use a non-standard key, in which case
		 *        they will always return a non-NULL value from this function
		 */
		virtual DataChunkPtr &GetKey(void) { return SpecifiedKey; }

		//! Get true if the default essence key has been overriden with a key that does not use GC track number mechanism
		/*  \note Defined EssenceSource sub-classes may always use a non-GC-type key, in which case
		 *        they will always return true from this function
		 */
		virtual bool GetNonGC(void) { return NonGC; }

		/* Essence type identification */
		/* These functions can be overwridden, or use the base versions to parse GetGCEssenceType() */

		//! Is this source a system item rather than an essence source?
		virtual bool IsSystemItem(void) { return false; }

		//! Is this source a generic stream item rather than an normal essence source?
		virtual bool IsGStreamItem(void) { return false; }


		//! Is this picture essence?
		virtual bool IsPictureEssence(void)
		{
			UInt8 Type = GetGCEssenceType();
			if((Type == 0x05) || (Type == 0x15)) return true;
			return false;
		}

		//! Is this sound essence?
		virtual bool IsSoundEssence(void)
		{
			UInt8 Type = GetGCEssenceType();
			if((Type == 0x06) || (Type == 0x16)) return true;
			return false;
		}

		//! Is this data essence?
		virtual bool IsDataEssence(void)
		{
			UInt8 Type = GetGCEssenceType();
			if((Type == 0x07) || (Type == 0x17)) return true;
			return false;
		}

		//! Is this compound essence?
		virtual bool IsCompoundEssence(void)
		{
			return (GetGCEssenceType() == 0x18);
		}


		//! An indication of the relative write order to use for this stream
		/*! Normally streams in a GC are ordered as follows:
		 *  - All the CP system items (in Scheme ID then Element ID order)
		 *  - All the GC system items (in Scheme ID then Element ID order)
		 *  - All the CP picture items (in Element ID then Element Number order)
		 *  - All the GC picture items (in Element ID then Element Number order)
		 *  - All the CP sound items (in Element ID then Element Number order)
		 *  - All the GC sound items (in Element ID then Element Number order)
		 *  - All the CP data items (in Element ID then Element Number order)
		 *  - All the GC data items (in Element ID then Element Number order)
		 *  - All the GC compound items (in Element ID then Element Number order) (no GC compound)
		 *
		 *  However, sometimes this order needs to be overridden - such as for VBI data preceding picture items.
		 *
		 *  The normal case for ordering of an essence stream is for RelativeWriteOrder to return 0,
		 *  indicating that the default ordering is to be used. Any other value indicates that relative
		 *  ordering is required, and this is used as the Position value for a SetRelativeWriteOrder()
		 *  call. The value of Type for that call is acquired from RelativeWriteOrderType()
		 *
		 * For example: to force a source to be written between the last GC sound item and the first CP data
		 *              item, RelativeWriteOrder() can return any -ve number, with RelativeWriteOrderType()
		 *				returning 0x07 (meaning before CP data). Alternatively RelativeWriteOrder() could
		 *				return a +ve number and RelativeWriteOrderType() return 0x16 (meaning after GC sound)
		 */
		virtual Int32 RelativeWriteOrder(void) { return 0; }

		//! The type for relative write-order positioning if RelativeWriteOrder() != 0
		/*! This method indicates the essence type to order this data before or after if reletive write-ordering is used
		 */
		virtual int RelativeWriteOrderType(void) { return 0; }

		//! Get the origin value to use for this essence specifically to take account of pre-charge
		/*! \return Zero if not applicable for this source
		 */
		virtual Length GetPrechargeSize(void) { return 0; }

		//! Get the range start position
		/*! \return Zero if not applicable for this source
		 */
		virtual Position GetRangeStart(void) { return 0; }

		//! Get the range end position7
		/*! \return -1 if not applicable for this source
		 */
		virtual Position GetRangeEnd(void) { return 0; }

		//! Get the range duration
		/*! \return -1 if not applicable for this source
		 */
		virtual Length GetRangeDuration(void) { return 0; }

		//! Get the name of this essence source (used for error messeges)
		virtual std::string Name(void) { return "Unnamed EssenceSource object"; }

		//! Enable VBR indexing, even in clip-wrap mode, by allowing each edit unit to be returned individually
		/*! As the byte offset part of a VBR index table is constructed at write-time each indexed chunk must be written separately.
		 *  When clip-wrapping, this is not normally the case as larger chunks may be returned for efficiency. Setting this mode
		 *  forces each indexable edit unit to be returned by a fresh GetEssenceData call as if it were being frame-wrapped.
		 *  \return true if this mode is supported by the source, and the mode was set, else false
		 */
		virtual bool EnableVBRIndexMode(void) { return false; }

		//! Set a length-to-send value
		void SetLenToSend( Length newVal ) { LenToSend=newVal; }

		//! Read the current length-to-send
		Length GetLenToSend( void ) const  { return LenToSend; }

		//! Attach a related System Item source to the owning BodyStream if required
		/*! DRAGONS: This is currently a non-ideal fudge - do not assume this method will last long!!! */
		virtual void AttachSystem(BodyStream *Stream)  { return; }


		/* Methods that apply to system item sources */

		//! Initialize this system item
		virtual void InitSystem(BodyStream *Stream) { return; }

		//! Get the number of KLVs in this system item
		virtual int GetSystemItemCount(void) { return 0; }

		//! Get the stream ID for the given system item KLV for this content package
		/*! \return The stream ID allocated to the item specified, or -1 if invalid
		 *  \param Item The 0-based item number
		 */
		virtual GCStreamID GetSystemItemID(int Item) { return -1; }

		//! Get the value for the given system item KLV for this content package
		/*! \return The value of the item specified, or NULL if invalid
		 *  \param Item The 0-based item number
		 */
		virtual DataChunkPtr GetSystemItemValue(int Item) { return NULL; }

		//! Set the essence descriptor
		virtual void SetDescriptor(MDObjectPtr Descriptor) { EssenceDescriptor = Descriptor; }

		//! Get a pointer to the essence descriptor for this source (if known) otherwise NULL
		virtual MDObjectPtr GetDescriptor(void) { return EssenceDescriptor; }

		//! Set the containing BodyStream
		void SetBodyStream(BodyStream *pBodyStream);
	};

	// Smart pointer to an EssenceSource object
	typedef SmartPtr<EssenceSource> EssenceSourcePtr;

	// Parent pointer to an EssenceSource object
	typedef ParentPtr<EssenceSource> EssenceSourceParent;

	// List of smart pointer to EssenceSource objects
	typedef std::list<EssenceSourcePtr> EssenceSourceList;
}


#include <cmath>

namespace mxflib
{
	//! Class for essence source that supplies system items
	class SystemSource : public EssenceSource
	{
	protected:
		EssenceSourceParent Master;								//!< The master stream for this essence
		GCStreamID SMPackID;									//!< Stream ID of the system metadata pack
		GCStreamID PMPackID;									//!< Stream ID of the package metadata pack
		UInt16 ContinuityCount;									//!< Continuity count as per SMPTE 385M
		int FPS;												//!< Integer frame rate value (25 or 30)
		bool Rate1001;											//!< True if FSP is a (n*1000) / 1001 rate
		UInt8 EssenceLabel[16];									//!< The essence container label
		bool DropFrame;											//!< True if timecode is using drop-frame counting
		UInt8 EssenceBitmap;									//!< Bitmap flags for essence items, bit 1 = 1 if data, bit 2 = 1 if sound, bit 3 = 1 if picture
		BodyStream *Stream;										//!< Our parent stream
		DataChunk CreationDate;									//!< A pre-formatted 17-byte chunk holding the creation date/time, or an empty chunk if not set
		DataChunk TimecodeData;									//!< A pre-formatted 17-byte chunk holding the timecode, or an empty chunk if not set
		DataChunk UMIDData;										//!< A pre-formatted chunk holding the UMID data for the Package Item, or an empty chunk if not set
		DataChunk KLVData;										//!< A pre-formatted chunk holding the KLV metadata for the Package Item, or an empty chunk if not set

	public:
		SystemSource(EssenceSource *MasterSource, const UL &WrappingUL) : EssenceSource()
		{
			Master = MasterSource;
			SMPackID = -1;
			PMPackID = -1;
			ContinuityCount = 0;
			EssenceBitmap = 0;
			Stream = NULL;

			// Record the frame rate and essence container label
			if(Master->GetEditRate().Denominator == 1)
			{
				FPS = Master->GetEditRate().Numerator;
			}
			else
			{
				double FloatFPS = static_cast<double>(Master->GetEditRate().Numerator) / static_cast<double>(Master->GetEditRate().Denominator);
				FPS = static_cast<int>(floor(FloatFPS + 0.5));
			}

			// Only set flag for exact (n*1000) / 1001 rate
			Rate1001 = (Master->GetEditRate().Denominator == 1001);

			// TODO: We don't yet set drop-frame for anything!
			DropFrame = false;

			memcpy(EssenceLabel, WrappingUL.GetValue(), 16);
		}

		//! Did the last call to GetEssenceData() return the end of a wrapping item
		/*! \return true if the last call to GetEssenceData() returned an entire wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the last chunk of a wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the end of a clip-wrapped clip.
		 *  \return false if there is more data pending for the current wrapping unit.
		 *  \return false if the source is to be clip-wrapped and there is more data pending for the clip
		 */
		virtual bool EndOfItem(void) { return true; }

		//! Is all data exhasted?
		/*! \return false if a call to GetEssenceData() will return some valid essence data
		 */
		virtual bool EndOfData(void) { if(Master) return Master->EndOfData(); else return true; }

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCEssenceType(void) { return 0x04; };

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCElementType(void) { return 0x00; }

		//! Get the edit rate of this wrapping of the essence
		/*! \note This may not be the same as the original "native" edit rate of the
		 *        essence if this EssenceSource is wrapping to a different edit rate
		 */
		virtual Rational GetEditRate(void) { if(Master) return Master->GetEditRate(); else return Rational(1,1); }

		//! Get the current position in GetEditRate() sized edit units
		/*! This is relative to the start of the stream, so the first edit unit is always 0.
		 *  This is the same as the number of edit units read so far, so when the essence is
		 *  exhausted the value returned shall be the size of the essence
		 */
		virtual Position GetCurrentPosition(void) { if(Master) return Master->GetCurrentPosition(); else return 0; }

		//! Get the preferred BER length size for essence KLVs written from this source, 0 for auto
		virtual int GetBERSize(void) { return 4; }

		//! Get BytesPerEditUnit, if Constant
		virtual UInt32 GetBytesPerEditUnit(UInt32 KAGSize = 1)
		{
			// Key, Len, standard value of 57 butes
			UInt32 Ret = 16 + GetBERSize() + 57;

			Ret += CalcFillerSize(Ret, KAGSize);

			return Ret;
		}

		//! Is this source a system item rather than an essence source?
		virtual bool IsSystemItem(void) { return true; }

		//! Is this picture essence?
		virtual bool IsPictureEssence(void) { return false; }

		//! Is this sound essence?
		virtual bool IsSoundEssence(void) { return false; }

		//! Is this data essence?
		virtual bool IsDataEssence(void) { return false; }

		//! Is this compound essence?
		virtual bool IsCompoundEssence(void) { return false; }

		//! Get the size of the essence data in bytes
		/*! \note There is intentionally no support for an "unknown" response
		 */
		virtual size_t GetEssenceDataSize(void) { return 0; }

		//! Get the next "installment" of essence data
		/*! \return Pointer to a data chunk holding the next data or a NULL pointer when no more remains
		 *	\note If there is more data to come but it is not currently available the return value will be a pointer to an empty data chunk
		 *	\note If Size = 0 the object will decide the size of the chunk to return
		 *	\note On no account will the returned chunk be larger than MaxSize (if MaxSize > 0)
		 */
		virtual DataChunkPtr GetEssenceData(size_t Size = 0, size_t MaxSize = 0) { return NULL; }


		/* Methods that apply to system item sources */

		//! Initialize this system item
		virtual void InitSystem(BodyStream *Stream);

		//! Get the number of KLVs in this system item
		virtual int GetSystemItemCount(void)
		{
			// Return 0 when all done (so we don't keep adding empty system items)
			if((!Master) || Master->EndOfData()) return 0;

			return 2;
		}

		//! Get the stream ID for the given system item KLV for this content package
		/*! \return The strema ID allocated to the item specified, or -1 if invalid
		 *  \param Item The 0-based item number
		 */
		virtual GCStreamID GetSystemItemID(int Item)
		{
			if(Item == 0) return SMPackID;
			else if(Item == 1) return PMPackID;
			return -1;
		}

		//! Get the value for the given system item KLV for this content package
		/*! \return The value of the item specified, or NULL if invalid
		 *  \param Item The 0-based item number
		 */
		virtual DataChunkPtr GetSystemItemValue(int Item);


		/* SystemSource Special functions */

		//! Set the creation date/time
		void SetCreationDateTime(std::string DateTime);

		//! Set the timecode from a string
		void SetTimecode(std::string TCString);


		//! Set the UMID to write in the Package Item (NULL will clear the UMID)
		void SetUMID(UMIDPtr Value = NULL);

		//! Set the KLV Metadata to write in the Package Item (NULL will clear the KLV Metadata)
		void SetKLVMetadata(MDObjectPtr Object = NULL);

	protected:
		//! Calculate the size of KLVFill required to align to the KAG from a given position
		UInt32 CalcFillerSize(Position FillPos, UInt32 KAGSize, bool ForceBER4 = false)
		{
			if(KAGSize == 0) KAGSize = 1;

			// Work out how far into a KAG we are
			UInt32 Offset = (UInt32)(FillPos % KAGSize);

			// Don't insert anything if we are already aligned
			if(Offset == 0) return 0;

			// Work out the required filler size
			UInt32 Fill = KAGSize - Offset;

			// Adjust so that the filler can fit
			// Note that for very small KAGs the filler may be several KAGs long
			if(ForceBER4)
			{
				while(Fill < 20) Fill += KAGSize;
			}
			else
			{
				while(Fill < 17) Fill += KAGSize;
			}

			if(Fill > 0x00ffffff)
			{
				error("Maximum supported filler is 0x00ffffff bytes long, "
					  "but attempt to fill from 0x%s to KAG of 0x%08x "
					  "requires a filler of size 0x%08x\n",
					  Int64toHexString(FillPos, 8).c_str(), KAGSize, Fill);
				Fill = 0x00ffffff;
			}

			return Fill;
		}


		//! Increment the internal timecode
		void IncrementTimecode(void);
	};
}


namespace mxflib
{
	//! Abstract super-class for objects that receive large quantities of essence data
	/*! \note Classes derived from this class <b>must not</b> include their own RefCount<> derivation
	 */
	class EssenceSink : public RefCount<EssenceSink>
	{
	protected:

	public:
		// Base constructor
		EssenceSink() {};

		//! Virtual destructor to allow polymorphism
		virtual ~EssenceSink() {};

		//! Receive the next "installment" of essence data
		/*! This will recieve a buffer containing thhe next bytes of essence data
		 *  \param Buffer The data buffer
		 *  \param BufferSize The number of bytes in the data buffer
		 *  \param EndOfItem This buffer is the last in this wrapping item
		 *  \return True if all is OK, else false
		 *  \note The first call may well fail if the sink has not been fully configured.
		 *	\note If false is returned the caller should make no more calls to this function, but the function should be implemented such that it is safe to do so
		 */
		virtual bool PutEssenceData(UInt8 const *Buffer, size_t BufferSize, bool EndOfItem = true) = 0;

		//! Receive the next "installment" of essence data from a smart pointer to a DataChunk
		bool PutEssenceData(DataChunkPtr &Buffer, bool EndOfItem = true) { return PutEssenceData(Buffer->Data, Buffer->Size, EndOfItem); }

		//! Receive the next "installment" of essence data from a DataChunk
		bool PutEssenceData(DataChunk &Buffer, bool EndOfItem = true) { return PutEssenceData(Buffer.Data, Buffer.Size, EndOfItem); }

		//! Called once all data exhasted
		/*! \return true if all is OK, else false
		 *  \note This function must also be called from the derived class' destructor in case it is never explicitly called
		 */
		virtual bool EndOfData(void) = 0;

		//! Get the name of this essence sink (used for error messeges)
		virtual std::string Name(void) { return "Unnamed EssenceSink object"; }

		//! Query the sink for an Int32 value
		virtual Int32 GetInt(std::string Query) { return 0; }

		//! Query the sink for a string value
		virtual std::string GetString(std::string Query) { return ""; }

		//! Set an Int32 value for this sink
		/*! /return true if the value was accepted, else false */
		virtual bool SetInt(std::string Text, Int32 Value) { return false; }

		//! Set a string value for this sink
		/*! /return true if the value was accepted, else false */
		virtual bool SetString(std::string Text, std::string Value) { return false; }
	};

	// Smart pointer to an EssenceSink object
	typedef SmartPtr<EssenceSink> EssenceSinkPtr;

	// Parent pointer to an EssenceSink object
	typedef ParentPtr<EssenceSink> EssenceSinkParent;

	// List of smart pointer to EssenceSink objects
	typedef std::list<EssenceSinkPtr> EssenceSinkList;
}



namespace mxflib
{
	//! Default "Multiple Essence Types in the Generic Container" Label
	const UInt8 GCMulti_Data[16] = { 0x06, 0x0E, 0x2B, 0x34, 0x04, 0x01, 0x01, 0x03, 0x0d, 0x01, 0x03, 0x01, 0x02, 0x7F, 0x01, 0x00 };
}


namespace mxflib
{
	//! Structure to hold information about each stream in a GC
	struct GCStreamData
	{
		DataChunkPtr SpecifiedKey;			//!< Non standard key to use, or NULL to use a standard key
		bool NonGC;							//!< True if the track number bytes are <b>not</b> to be set automatically
		UInt8 Type;							//!< Item type
		UInt8 SchemeOrCount;				//!< Scheme if system or element count if essence
		UInt8 Element;						//!< Element identifier or type
		UInt8 SubOrNumber;					//!< Sub ID if system or element number if essence
		UInt8 RegDes;						//!< The registry designator if this is a system item
		UInt8 RegVer;						//!< The registry version number for the item key
		int LenSize;						//!< The KLV length size to use for this stream (0 for auto)
		IndexManagerPtr IndexMan;			//!< If indexing this stream a pointer to the index manager, else NULL
		int IndexSubStream;					//!< If indexing this stream the sub stream number, else undefined
		bool IndexFiller;					//!< If indexing this stream true if filler <b>preceeding</b> this stream is to be indexed, else undefined
		bool IndexClip;						//!< True if indexing clip-wrapped essence
		bool CountFixed;					//!< True once the essence element count has been fixed
											/*!< The count is fixed the first time either a key is written
											 *   or a track number is reported */
		UInt32 WriteOrder;					//!< The (default) write order for this stream
											/*!< Elements with a lower WriteOrder are written first when the
											 *   content package is written */
	};

	//! Class that manages writing of generic container essence
	class GCWriter : public RefCount<GCWriter>
	{
	protected:
		MXFFilePtr LinkedFile;				//!< File that will be written to
		UInt32 TheBodySID;					//!< Body SID for this Essence Container

		int	StreamTableSize;				//!< Size of StreamTable
		int	StreamCount;					//!< Number of entries in use in StreamTable

		int StreamBase;						//!< Base of all stream numbers in keys

		GCStreamData *StreamTable;			//!< Table of data for streams for this GC

		UInt32 KAGSize;						//!< KAGSize for this Essence Container
		bool ForceFillerBER4;				//!< True if filler items must have BER lengths forced to 4-byte BER

		Int32 NextWriteOrder;				//!< The "WriteOrder" to use for the next auto "SetWriteOrder()"

		Position IndexEditUnit;				//!< Edit unit of the current CP for use if indexing
											/*!< This property starts at zero and is incremented with each CP written, however the value
											 *   can be changed by calling SetIndexEditUnit() before calling StartNewCP()
											 */

		Length PreCharge;					//!< The number of edit units of pre-charge at the start of the essence (required for indexing)

		UInt64 StreamOffset;				//!< Current stream offset within this essence container

		//! Map of all used write orders to stream ID - used to ensure no duplicates
		std::map<UInt32, GCStreamID> WriteOrderMap;


	public:
		//! Constructor
		GCWriter(MXFFilePtr File, UInt32 BodySID = 0, int Base = 1);

		//! Destructor
		~GCWriter();

		//! Set the KAG for this Essence Container
		void SetKAG(UInt32 KAG, bool ForceBER4 = false) { KAGSize = KAG; ForceFillerBER4 = ForceBER4; };

		//! Get the current KAGSize
		UInt32 GetKAG(void) { return KAGSize; }

		//! Define a new non-CP system element for this container
		GCStreamID AddSystemElement(unsigned int RegistryDesignator, unsigned int SchemeID, unsigned int ElementID, unsigned int SubID = 0)	{ return AddSystemElement(false, RegistryDesignator, SchemeID, ElementID, SubID); }

		//! Define a new CP-compatible system element for this container
		GCStreamID AddCPSystemElement(unsigned int RegistryDesignator, unsigned int SchemeID, unsigned int ElementID, unsigned int SubID = 0)	{ return AddSystemElement(true, RegistryDesignator, SchemeID, ElementID, SubID); }

		//! Define a new system element for this container
		GCStreamID AddSystemElement(bool CPCompatible, unsigned int RegistryDesignator, unsigned int SchemeID, unsigned int ElementID, unsigned int SubID = 0);

		//! Define a new non-CP picture element for this container
		GCStreamID AddPictureElement(unsigned int ElementType) { return AddPictureElement(false, ElementType); }

		//! Define a new CP-compatible picture element for this container
		GCStreamID AddCPPictureElement(unsigned int ElementType) { return AddPictureElement(true, ElementType); }

		//! Define a new picture element for this container
		GCStreamID AddPictureElement(bool CPCompatible, unsigned int ElementType) { return AddEssenceElement( CPCompatible ? 0x05 : 0x15, ElementType); }

		//! Define a new non-CP sound element for this container
		GCStreamID AddSoundElement(unsigned int ElementType) { return AddPictureElement(false, ElementType); }

		//! Define a new CP-compatible sound element for this container
		GCStreamID AddCPSoundElement(unsigned int ElementType) { return AddPictureElement(true, ElementType); }

		//! Define a new sound element for this container
		GCStreamID AddSoundElement(bool CPCompatible, unsigned int ElementType) { return AddEssenceElement( CPCompatible ? 0x06 : 0x16, ElementType); }

		//! Define a new non-CP data element for this container
		GCStreamID AddDataElement(unsigned int ElementType) { return AddDataElement(false, ElementType); }

		//! Define a new CP-compatible data element for this container
		GCStreamID AddCPDataElement(unsigned int ElementType) { return AddDataElement(true, ElementType); }

		//! Define a new data element for this container
		GCStreamID AddDataElement(bool CPCompatible, unsigned int ElementType) { return AddEssenceElement( CPCompatible ? 0x07 : 0x17, ElementType); }

		//! Define a new compound element for this container
		GCStreamID AddCompoundElement(unsigned int ElementType) { return AddEssenceElement( 0x18, ElementType); }

		//! Define a new essence element for this container
		GCStreamID AddEssenceElement(unsigned int EssenceType, unsigned int ElementType, int LenSize = 0);

		//! Define a new essence element for this container, with a specified key
		GCStreamID AddEssenceElement(DataChunkPtr &Key, int LenSize = 0, bool NonGC = false);

		//! Define a new essence element for this container, with a specified key
		GCStreamID AddEssenceElement(int KeySize, UInt8 *KeyData, int LenSize = 0, bool NonGC = false)
		{
			DataChunkPtr Key = new DataChunk(KeySize, KeyData);
			return AddEssenceElement(Key, LenSize, NonGC);
		}

		//! Allow this data stream to be indexed and set the index manager
		void AddStreamIndex(GCStreamID ID, IndexManagerPtr &IndexMan, int IndexSubStream, bool IndexFiller = false, bool IndexClip = false);

		//! Get the track number associated with the specified stream
		UInt32 GetTrackNumber(GCStreamID ID);

		//! Assign an essence container (mapping) UL to the specified stream
		void AssignEssenceUL(GCStreamID ID, ULPtr EssenceUL);

		//! Start a new content package (and write out the prevous one if required)
		void StartNewCP(void);

		//! Calculate how much data will be written if "Flush" is called now
		UInt64 CalcWriteSize(void);

		//! Flush any remaining data
		void Flush(void);

		//! Get the current stream offset
		Int64 GetStreamOffset(void) { return StreamOffset; }

		//! Set the index position for the current CP
		void SetIndexEditUnit(Position EditUnit) { IndexEditUnit = EditUnit; }

		//! Set the pre-charge size to allow the index table to be built correctly
		void SetPreCharge(Length PreChargeSize)
		{
			// Record for any new streams
			PreCharge = PreChargeSize;

			// Set any existing streams
			for(int i=0; i<StreamCount; i++) if(StreamTable[i].IndexMan) StreamTable[i].IndexMan->SetPreCharge(PreChargeSize);
		}

		//! Get the index position of the current CP
		Position GetIndexEditUnit(void) { return IndexEditUnit; }

		//! Add system item data to the current CP
		void AddSystemData(GCStreamID ID, UInt64 Size, const UInt8 *Data);

		//! Add system item data to the current CP
		void AddSystemData(GCStreamID ID, DataChunkPtr Chunk) { AddSystemData(ID, Chunk->Size, Chunk->Data); }

		//! Add encrypted system item data to the current CP
		void AddSystemData(GCStreamID ID, UInt64 Size, const UInt8 *Data, UUIDPtr ContextID, Length PlaintextOffset = 0);

		//! Add encrypted system item data to the current CP
		void AddSystemData(GCStreamID ID, DataChunkPtr Chunk, UUIDPtr ContextID, Length PlaintextOffset = 0) { AddSystemData(ID, Chunk->Size, Chunk->Data, ContextID, PlaintextOffset); }

		//! Add essence data to the current CP
		void AddEssenceData(GCStreamID ID, UInt64 Size, const UInt8 *Data, BodyStreamPtr BStream = NULL);

		//! Add essence data to the current CP
		void AddEssenceData(GCStreamID ID, DataChunkPtr Chunk, BodyStreamPtr BStream = NULL) { AddEssenceData(ID, Chunk->Size, Chunk->Data, BStream); }

		//! Add essence data to the current CP
		void AddEssenceData(GCStreamID ID, EssenceSourcePtr Source, bool FastClipWrap = false, BodyStreamPtr BStream = NULL);

/*		//! Add encrypted essence data to the current CP
		void AddEssenceData(GCStreamID ID, UInt64 Size, const UInt8 *Data, UUIDPtr ContextID, Length PlaintextOffset = 0);

		//! Add encrypted essence data to the current CP
		void AddEssenceData(GCStreamID ID, DataChunkPtr Chunk, UUIDPtr ContextID, Length PlaintextOffset = 0)  { AddEssenceData(ID, Chunk->Size, Chunk->Data, ContextID, PlaintextOffset); }

		//! Add encrypted essence data to the current CP
		void AddEssenceData(GCStreamID ID, EssenceSource* Source, UUIDPtr ContextID, Length PlaintextOffset = 0, bool FastClipWrap = false);
*/
		//! Add an essence item to the current CP with the essence to be read from a KLVObject
		void AddEssenceData(GCStreamID ID, KLVObjectPtr Source, bool FastClipWrap = false, BodyStreamPtr BStream = NULL);


		//! Calculate how many bytes would be written if the specified object were written with WriteRaw()
		Length CalcRawSize(KLVObjectPtr Object);

		//! Write a raw KLVObject to the file - this is written immediately and not buffered in the WriteQueue
		void WriteRaw(KLVObjectPtr Object);


		//! Structure for items to be written
		struct WriteBlock
		{
			UInt64 Size;				//!< Number of bytes of data to write
			UInt8 *Buffer;				//!< Pointer to bytes to write
			EssenceSourcePtr Source;	//!< Smart pointer to an EssenceSource object or NULL
			KLVObjectPtr KLVSource;		//!< Pointer to a KLVObject as source - or NULL
			int LenSize;				//!< The KLV length size to use for this item (0 for auto)
			IndexManagerPtr IndexMan;	//!< Index manager that wants to know about this data
			BodyStreamPtr Stream;		//!< The calling BodyStream object - or NULL
			int IndexSubStream;			//!< Sub-stream ID of data for indexing
			bool IndexFiller;			//!< If true filler will also be indexed with SubStream -1
			bool IndexClip;				//!< True if indexing clip-wrapped essence
			bool WriteEncrypted;		//!< True if the data is to be written as encrypted data (via a KLVEObject)
			bool FastClipWrap;			//!< True if this KLV is to be "FastClipWrapped"
		};

		//! Type for holding the write queue in write order
		typedef std::map<UInt32,WriteBlock> WriteQueueMap;

		//! Queue of items for the current content package in write order
		WriteQueueMap WriteQueue;


		//! Set the WriteOrder for the specified stream
		void SetWriteOrder(GCStreamID ID, Int32 WriteOrder = -1, int Type =-1);

		//! Set a write-order relative to all items of a specified type
		void SetRelativeWriteOrder(GCStreamID ID, int Type, Int32 Position);

		//! Get the WriteOrder for the specified stream
		Int32 GetWriteOrder(GCStreamID ID);

		//! Read the count of streams
		int GetStreamCount(void) { return StreamCount; };
	};
}


namespace mxflib
{
	//! Class for holding a description of an essence stream
	class EssenceStreamDescriptor;

	//! A smart pointer to a EssenceStreamDescriptor object
	typedef SmartPtr<EssenceStreamDescriptor> EssenceStreamDescriptorPtr;

	//! A list of smart pointers to EssenceStreamDescriptor objects
	typedef std::list<EssenceStreamDescriptorPtr> EssenceStreamDescriptorList;

	/*! \page SubStreamNotes Notes on Sub-Streams
	 *  \section SubStreams1 Sub-Streams Introduction
	 *  Certain essence streams may have intimate data related to the essence that is linked as a substream.
	 *  \section SubParserSubStreams Sub-Streams in EssenceSubParsers
	 *  An EssenceSubParser may produce a main EssenceSource with sub-streams which are EssenceSources whose
	 *  data is extracted during the parsing that produces the main source's data. These SubStreams are indicated
	 *  by members of the EssenceStreamDescriptor::SubStreams properties of members of the EssenceStreamDescriptorList
	 *  returned by a call to EssenceSubParserBase::IdentifyEssence(). This in turn gets propogated to the
	 *  EssenceParser::WrapingConfig::SubStreams properties of members of the EssenceParser::WrapingConfigList
	 *  returned by a call to EssenceParser::ListWrappingOptions().
	 *
	 *  The value of EssenceStreamDescriptor::ID, and hence EssenceParser::WrapingConfig::Stream, will differ
	 *  between the main stream and its sub-streams. These stream IDs are passed to EssenceSubParserBase::GetEssenceSource
	 *  to produce the desired EssenceSource objects.  The master stream needs to be requested first, otherwise the
	 *  EssenceSubParserBase::GetEssenceSource is unlikely to produce a valid sub-stream EssenceSource.
	 *
	 *  It is worth noting that as the sub-stream data is extracted from the master stream, the master stream is responsible
	 *  for managing the file handle and other items such as the edit rate.
	 *
	 *  \attention Any EssenceSubParser providing sub-streams <b>must</b> support EssenceSubParserBase::ReValidate(),
	 *             even if only to reject all attempts to continue into the next file (as this may not be a valid thing to do).
	 *
	 *  \attention It is the responsibility of the EssenceSubParser to ensure that data for all streams is extracted from
	 *             the initial file before the master stream returns NULL from its GetEssenceData() method.
	 *             This is because the file will be closed soon after that call is made.
	 *
	 *  DRAGONS: There may be a requirement at some point to allow an EssenceSubParser to keep the file open if a huge amount of data is still unread
	 */

	/*! Class for holding a description of an essence stream
	 *  (used to differentiate multiple streams in an essence file)
	 *  and a human-readable description
	 */
	class EssenceStreamDescriptor : public RefCount<EssenceStreamDescriptor>
	{
	public:
		//! Construct with any optional items cleared
		EssenceStreamDescriptor() : StartTimecode(0) {}

	public:
		UInt32 ID;								//!< ID for this essence stream
		std::string Description;				//!< Description of this essence stream
		UUID SourceFormat;						//!< A UUID (or byte-swapped UL) identifying the source format
		MDObjectPtr Descriptor;					//!< Pointer to an actual essence descriptor for this stream
		EssenceStreamDescriptorList SubStreams;	//!< A list of sub-streams that can be derived from this stream. See \ref SubStreamNotes
		Position StartTimecode;					//!< The starting timecode of this essence, if known, or zero
	};


	//! Base class for any EssenceSubParserFactory classes
	class EssenceSubParserFactory : public RefCount<EssenceSubParserFactory>
	{
	public:
		//! Build a new sub-parser of the appropriate type
		virtual EssenceSubParserPtr NewParser(void) const = 0;
	};

	//! Smart pointer to an EssenceSubParserFactory
	typedef SmartPtr<EssenceSubParserFactory> EssenceSubParserFactoryPtr;


	//! Abstract base class for all essence parsers
	/*! \note It is important that no derived class has its own derivation of RefCount<> */
	class EssenceSubParser : public RefCount<EssenceSubParser>
	{
	protected:
		//! The wrapping options selected
		WrappingOptionPtr SelectedWrapping;

		//! The index manager in use
		IndexManagerPtr Manager;

		//! This essence stream's stream ID in the index manager
		int	ManagedStreamID;

		//! The essence descriptor describing this essence (if known) else NULL
		MDObjectPtr EssenceDescriptor;

	public:

		//! Base class for essence parser EssenceSource objects
		/*! Still abstract as there is no generic way to determine the data size */
		class ESP_EssenceSource : public EssenceSource
		{
		protected:
			EssenceSubParserPtr Caller;
			FileHandle File;
			UInt32 Stream;
			UInt64 RequestedCount;
			IndexTablePtr Index;
			DataChunkPtr RemainingData;
			bool AtEndOfData;
			bool Started;

		public:
			//! Construct and initialise for essence parsing/sourcing
			ESP_EssenceSource(EssenceSubParserPtr TheCaller, FileHandle InFile, UInt32 UseStream, UInt64 Count = 1)
			{
				Caller = TheCaller;
				File = InFile;
				Stream = UseStream;
				RequestedCount = Count;
				AtEndOfData = false;
				Started = false;
			};

			//! Get the next "installment" of essence data
			/*! This will attempt to return an entire wrapping unit (e.g. a full frame for frame-wrapping) but will return it in
			 *  smaller chunks if this would break the MaxSize limit. If a Size is specified then the chunk returned will end at
			 *  the first wrapping unit end encountered before Size. On no account will portions of two or more different wrapping
			 *  units be returned together. The mechanism for selecting a type of wrapping (e.g. frame, line or clip) is not
			 *  (currently) part of the common EssenceSource interface.
			 *  \return Pointer to a data chunk holding the next data or a NULL pointer when no more remains
			 *	\note If there is more data to come but it is not currently available the return value will be a pointer to an empty data chunk
			 *	\note If Size = 0 the object will decide the size of the chunk to return
			 *	\note On no account will the returned chunk be larger than MaxSize (if MaxSize > 0)
			 */
			virtual DataChunkPtr GetEssenceData(size_t Size = 0, size_t MaxSize = 0) { return BaseGetEssenceData(Size, MaxSize); };

			//! Non-virtual basic version of GetEssenceData() that can be called by derived classes
			/*! DRAGONS: This implementation always reads whole wrapping units, so it NOT SAFE if these could be too large to fit in memory
			 */
			DataChunkPtr BaseGetEssenceData(size_t Size = 0, size_t MaxSize = 0)
			{
				// Allow us to differentiate the first call
				if(!Started) Started = true;

				DataChunkPtr Data;

				if(RemainingData)
				{
					Data = RemainingData;
					RemainingData = NULL;
				}
				else
				{
					Data = Caller->Read(File, Stream, 1);
				}
				if(Data)
				{
					if(Data->Size == 0) Data = NULL;
					else
					{
						if((MaxSize) && (Data->Size > MaxSize))
						{
							RemainingData = new DataChunk(Data->Size - MaxSize, &Data->Data[MaxSize]);
							Data->Resize((UInt32)MaxSize);
						}
					}
				}

				// Record when we hit the end of all data
				if(!Data) AtEndOfData = true;

				return Data;
			}

			//! Did the last call to GetEssenceData() return the end of a wrapping item
			/*! \return true if the last call to GetEssenceData() returned an entire wrapping unit.
			 *  \return true if the last call to GetEssenceData() returned the last chunk of a wrapping unit.
			 *  \return true if the last call to GetEssenceData() returned the end of a clip-wrapped clip.
			 *  \return false if there is more data pending for the current wrapping unit.
			 *  \return false if the source is to be clip-wrapped and there is more data pending for the clip
			 */
			virtual bool EndOfItem(void)
			{
				// If we are clip wrapping then we only end when no more data
				if(Caller->GetWrapType() == WrappingOption::Clip) return AtEndOfData;

				// Otherwise items end when there is no data remaining from the last read
				return !RemainingData;
			}

			//! Is all data exhasted?
			/*! \return true if a call to GetEssenceData() will return some valid essence data
			 */
			virtual bool EndOfData(void) { return AtEndOfData; }

			//! Get data to write as padding after all real essence data has been processed
			/*! If more than one stream is being wrapped, they may not all end at the same wrapping-unit.
			 *	When this happens each source that has ended will produce NULL is response to GetEssenceData().
			 *	The default action of the caller would be to write zero-length KLVs in each wrapping unit for each source that has ended.
			 *	If a source supplies an overload for this method, the supplied padding data will be written in wrapping units following the end of essence instead of a zero-length KLV
			 *	DRAGONS: Note that as the returned value is a non-smart pointer, ownership of the buffer stays with the EssenceSource object.
			 *			 The recommended method of operation is to have a member DataChunk (or DataChunkPtr) allocated the first time padding is required, and return the address each call.
			 *			 The destructor must then free the DataChunk, or allow the smart DataChunkPtr to do it automatically
			 */
			virtual DataChunk *GetPadding(void) { return NULL; }

			//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
			virtual UInt8 GetGCEssenceType(void) { return Caller->GetGCEssenceType(); }

			//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
			virtual UInt8 GetGCElementType(void) { return Caller->GetGCElementType(); }

			//! Is the last data read the start of an edit point?
			virtual bool IsEditPoint(void) { return true; }

			//! Get the edit rate of this wrapping of the essence
			/*! \note This may not be the same as the original "native" edit rate of the
			 *        essence if this EssenceSource is wrapping to a different edit rate
			 */
			virtual Rational GetEditRate(void) { return Caller->GetEditRate(); }

			//! Get the current position in GetEditRate() sized edit units
			/*! This is relative to the start of the stream, so the first edit unit is always 0.
			 *  This is the same as the number of edit units read so far, so when the essence is
			 *  exhausted the value returned shall be the size of the essence
			 */
			virtual Position GetCurrentPosition(void) { return Caller->GetCurrentPosition(); }

			//! Set a parser specific option
			/*! \return true if the option was successfully set */
			virtual bool SetOption(std::string Option, Int64 Param = 0) { return Caller->SetOption(Option, Param); } ;

			//! Get BytesPerEditUnit if Constant, else 0
			/*! \note This value may be useful even if CanIndex() returns false
			 */
			virtual UInt32 GetBytesPerEditUnit(UInt32 KAGSize = 1) { return Caller->GetBytesPerEditUnit(KAGSize); }

			//! Can this stream provide indexing
			/*! If true then SetIndex Manager can be used to set the index manager that will receive indexing data
			 */
			virtual bool CanIndex() { return Caller->SelectedWrapping->CanIndex; }

			//! Set the index manager to use for building index tables for this essence
			virtual void SetIndexManager(IndexManagerPtr &Manager, int StreamID)
			{
				// Set the manager in our containing parser
				Caller->SetIndexManager(Manager, StreamID);
			}

			//! Get the index manager
			virtual IndexManagerPtr &GetIndexManager(void)
			{
				// Set the manager in our containing parser
				return Caller->GetIndexManager();
			}

			//! Get the IndexManager StreamID for this essence stream
			virtual int GetIndexStreamID(void)
			{
				if(Caller)
				{
					// Get from our containing parser
					return Caller->GetIndexStreamID();
				}
					return IndexStreamID;
			}

			//! Get the name of this essence source (used for error messeges)
			virtual std::string Name(void)
			{
				if(Caller)
					 return Caller->GetParserName() +" sub-parser";
				else
					return " Parser";
			}

			//! Set the essence descriptor
			virtual void SetDescriptor(MDObjectPtr Descriptor) { Caller->EssenceDescriptor = Descriptor; }

			//! Get a pointer to the essence descriptor for this source (if known) otherwise NULL
			virtual MDObjectPtr GetDescriptor(void) { return Caller->EssenceDescriptor; }
		};

		// Allow embedded essence source to access our protected properties
		friend class ESP_EssenceSource;


	protected:

	public:
		//! Base destructor (to allow polymorphism)
		virtual ~EssenceSubParser() {};

		//! Report the extensions of files this sub-parser is likely to handle
		virtual StringList HandledExtensions(void) { StringList Ret; return Ret; };

		//! Examine the open file and return a list of essence descriptors
		/*! This function should fail as fast as possible if the essence if not identifyable by this object
		 *	\return A list of EssenceStreamDescriptors where each essence stream identified in the input file has
		 *			an identifier (to allow it to be referenced later) and an MXF File Descriptor
		 */
		virtual EssenceStreamDescriptorList IdentifyEssence(FileHandle InFile)
		{
			EssenceStreamDescriptorList Ret;
			return Ret;
		}

		//! Examine the open file and return the wrapping options known by this parser
		/*! \param InFile The open file to examine (if the descriptor does not contain enough info)
		 *	\param Descriptor An essence stream descriptor (as produced by function IdentifyEssence)
		 *		   of the essence stream requiring wrapping
		 *	\note The options should be returned in an order of preference as the caller is likely to use the first that it can support
		 */
		virtual WrappingOptionList IdentifyWrappingOptions(FileHandle InFile, EssenceStreamDescriptor &Descriptor)
		{
			WrappingOptionList Ret;
			return Ret;
		}

		//! Set a wrapping option for future Read and Write calls
		virtual void Use(UInt32 Stream, WrappingOptionPtr &UseWrapping)
		{
			// DRAGONS: Any derived version of Use() must also set SelectedWrapping
			SelectedWrapping = UseWrapping;
		}

		//! Does this essence parser support ReValidate()
		virtual bool CanReValidate(void) { return false; }

		//! Quickly validate that the given (open) file can be wrapped as specified
		/*! Providing that the given essence descriptor and wrapping option can be used this will leave the parser
		 *  in the state it would have been in after calls to IdentifyEssence(), IdentifyWrappingOptions() and Use().
		 *  This is used when parsing a list of files to check that the second and later files are the same format as the first
		 *  \return true if all OK
		 */
		virtual bool ReValidate(FileHandle Infile, UInt32 Stream, MDObjectPtr &Descriptor, WrappingOptionPtr &UseWrapping)
		{
			return false;
		}

		//! Get the wrapping type that has been selected by Use()
		WrappingOption::WrapType GetWrapType(void)
		{
			if(!SelectedWrapping) return WrappingOption::None;

			return SelectedWrapping->ThisWrapType;
		}

		//! Set a non-native edit rate
		/*! \return true if this rate is acceptable */
		virtual bool SetEditRate(Rational EditRate)
		{
			// Default action is to not allow the edit rate to be changed
			return (EditRate == GetEditRate());
		}

		//! Get the current edit rate
		virtual Rational GetEditRate(void) = 0;

		//! Get the preferred edit rate (if one is known)
		/*! \return The prefered edit rate or 0/0 if note known
		 */
		virtual Rational GetPreferredEditRate(void)
		{
			// By default we don't know the preferred rate
			return Rational(0,0);
		}

		//! Get BytesPerEditUnit, if Constant
		/*! Note that we use KAGSize to prevent compiler warnings (we cannot omit it as it has a default value) */
		virtual UInt32 GetBytesPerEditUnit(UInt32 KAGSize = 1) { return KAGSize * 0; }

		//! Get the current position in SetEditRate() sized edit units
		/*! This is relative to the start of the stream, so the first edit unit is always 0.
		 *  This is the same as the number of edit units read so far, so when the essence is
		 *  exhausted the value returned shall be the size of the essence
		 */
		virtual Position GetCurrentPosition(void) = 0;

		//! Set the IndexManager for this essence stream (and the stream ID if we are not the main stream)
		virtual void SetIndexManager(IndexManagerPtr &TheManager, int StreamID = 0)
		{
			Manager = TheManager;
			ManagedStreamID = StreamID;
		}

		//! Get the IndexManager for this essence stream
		virtual IndexManagerPtr &GetIndexManager(void) { return Manager; };

		//! Get the IndexManager StreamID for this essence stream
		virtual int GetIndexStreamID(void) { return ManagedStreamID; };

		//! Set the stream offset for a specified edit unit into the current index manager
		virtual void SetStreamOffset(Position EditUnit, UInt64 Offset)
		{
			if(Manager) Manager->SetOffset(ManagedStreamID, EditUnit, Offset);
		}

		//! Offer the stream offset for a specified edit unit to the current index manager
		virtual bool OfferStreamOffset(Position EditUnit, UInt64 Offset)
		{
			if(!Manager) return false;
			return Manager->OfferOffset(ManagedStreamID, EditUnit, Offset);
		}

		//! Instruct index manager to accept the next edit unit
		virtual void IndexNext(void)
		{
			if(Manager) Manager->AcceptNext();
		}

		//! Instruct index manager to accept and log the next edit unit
		virtual int IndexLogNext(void)
		{
			if(Manager) return Manager->AcceptLogNext();
			return -1;
		}

		//! Instruct index manager to log the next edit unit
		virtual int LogNext(void)
		{
			if(Manager) return Manager->LogNext();
			return -1;
		}

		//! Read an edit unit from the index manager's log
		virtual Position ReadLog(int LogID)
		{
			if(Manager) return Manager->ReadLog(LogID);
			return IndexTable::IndexLowest;
		}

		//! Instruct index manager to accept provisional entry
		/*! \return The edit unit of the entry accepted - or IndexLowest if none available */
		virtual Position AcceptProvisional(void)
		{
			if(Manager) return Manager->AcceptProvisional();
			return IndexTable::IndexLowest;
		}

		//! Read the edit unit of the last entry added via the index manager (or IndexLowest if none added)
		Position GetLastNewEditUnit(void)
		{
			if(Manager) return Manager->GetLastNewEditUnit();
			return IndexTable::IndexLowest;
		}

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCEssenceType(void) { return SelectedWrapping->GCEssenceType; }

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCElementType(void) { return SelectedWrapping->GCElementType; }


		//! Read a number of wrapping items from the specified stream and return them in a data chunk
		/*! If frame or line mapping is used the parameter Count is used to
		 *	determine how many items are read. In frame wrapping it is in
		 *	units of EditRate, as specified in the call to Use(), which may
		 *  not be the frame rate of this essence
		 *	\note This is going to take a lot of memory in clip wrapping!
		 */
		virtual DataChunkPtr Read(FileHandle InFile, UInt32 Stream, UInt64 Count = 1) = 0;

		//! Build an EssenceSource to read a number of wrapping items from the specified stream
		virtual EssenceSourcePtr GetEssenceSource(FileHandle InFile, UInt32 Stream, UInt64 Count = 1) = 0;

		//! Write a number of wrapping items from the specified stream to an MXF file
		/*! If frame or line mapping is used the parameter Count is used to
		 *	determine how many items are read. In frame wrapping it is in
		 *	units of EditRate, as specified in the call to Use(), which may
		 *  not be the frame rate of this essence stream
		 *	\note This used to be the only safe option for clip wrapping
		 *	\return Count of bytes transferred
		 *  DRAGONS: ** DEPRECATED **
		 */
		virtual Length Write(FileHandle InFile, UInt32 Stream, MXFFilePtr OutFile, UInt64 Count = 1) = 0;

		//! Set a parser specific option
		/*! \return true if the option was successfully set */
		virtual bool SetOption(std::string Option, Int64 Param = 0) { return false; } ;

		//! Get a unique name for this sub-parser
		/*! The name must be all lower case, and must be unique.
		 *  The recommended name is the part of the filename of the parser header after "esp_" and before the ".h".
		 *  If the parser has no name return "" (however this will prevent named wrapping option selection for this sub-parser)
		 */
		virtual std::string GetParserName(void) const { return ""; }

		//! Build a new sub-parser of the appropriate type
		/*! \note You must redefine this function in a sub-parser even if it is not going to be its own factory (EssenceSubParserFactory used).
		 *        In this case it is best to call the factory's NewParser() method.
		 */
		virtual EssenceSubParserPtr NewParser(void) const = 0;

		//! Set the essence descriptor
		virtual void SetDescriptor(MDObjectPtr Descriptor) { EssenceDescriptor = Descriptor; }

		//! Get a pointer to the essence descriptor for this source (if known) otherwise NULL
		virtual MDObjectPtr GetDescriptor(void) { return EssenceDescriptor; }
	};

	//! Rename of EssenceSubParser for legacy compatibility
	typedef EssenceSubParser EssenceSubParserBase;



	//! A wrapper class that allows an EssenceSubParser to be its own factory
	/*! This less memory-efficient method supports older EssenceSubParsers
	 */
	class EssenceSubParserSelfFactory : public EssenceSubParserFactory
	{
	protected:
		//! The parser that we are wrapping
		EssenceSubParserPtr Parser;

	public:
		//! Construct a factory that wraps a specified self-factory parser
		EssenceSubParserSelfFactory(EssenceSubParserPtr Parser) : Parser(Parser) {};

		//! Build a new sub-parser of the appropriate type
		virtual EssenceSubParserPtr NewParser(void) const { return Parser->NewParser(); }
	};

}


namespace mxflib
{
	//! Pair containing a pointer to an essence parser and its associated essence descriptors
	typedef std::pair<EssenceSubParserPtr, EssenceStreamDescriptorList> ParserDescriptorPair;
	//! List of pointers to essence parsers
	typedef std::list<EssenceSubParserPtr> EssenceParserList;


	//! List of pairs of essence parser pointers with associated file descriptors
	class ParserDescriptorList : public RefCount<ParserDescriptorList>, public std::list<ParserDescriptorPair> {};
	typedef SmartPtr<ParserDescriptorList> ParserDescriptorListPtr;


	//! Master-class for parsing essence via EssenceSubParser objects
	class EssenceParser
	{
	public:
		//! A list of parser factory functions
		typedef std::list<EssenceSubParserFactoryPtr> EssenceSubParserFactoryList;

	protected:
		//! List of pointers to known parsers
		/*! Used only for building parsers to parse essence - the parses
		 *  in this list must not themselves be used for essence parsing
		 */
		static EssenceSubParserFactoryList EPList;

		//! Initialization flag for EPList
		static bool Inited;

	private:
		//! Prevent instantiation of essence parser - all methods are now static
		EssenceParser();

	public:
		//! Add a new EssenceSubParser type
		/*! This adds a factory to build instances of a new sub parser type if required
		 *  to parse an essence stream
		 */
		static void AddNewSubParserType(EssenceSubParserFactoryPtr Factory)
		{
			EPList.push_back(Factory);
		}

		//! Add a new EssenceSubParser type
		/*! This adds a factory to build instances of a new sub parser type if required
		 *  to parse an essence stream.
		 *  \note This is the lecacy version to cope with EssenceSubParsers which are thier own factories
		 */
		static void AddNewSubParserType(EssenceSubParserPtr SubParser)
		{
			EssenceSubParserFactoryPtr Factory = new EssenceSubParserSelfFactory(SubParser);

			EPList.push_back(Factory);
		}

		//! Build a list of parsers with their descriptors for a given essence file
		static ParserDescriptorListPtr IdentifyEssence(FileHandle InFile);

		//! Configuration data for an essence parser with a specific wrapping option
		class WrappingConfig;

		//! Smart pointer to a WrappingConfig object
		typedef SmartPtr<WrappingConfig> WrappingConfigPtr;

		//! Parent pointer to a WrappingConfig object
		typedef ParentPtr<WrappingConfig> WrappingConfigParent;

		//! List of smart pointers to WrappingConfig objects
		typedef std::list<WrappingConfigPtr> WrappingConfigList;

		//! Configuration data for an essence parser with a specific wrapping option
		/*! \note No parser may contain one of these that includes a pointer to that parser otherwise it will never be deleted (circular reference)
		 */
		class WrappingConfig : public RefCount<WrappingConfig>
		{
		public:
			//! Construct with any optional items cleared
			WrappingConfig() : StartTimecode(0), IsExternal(false), KAGSize(1), File(FileInvalid) {}

		public:
			EssenceSubParserPtr Parser;					//!< The parser that parses this essence - true smart pointer not a parent pointer to keep parser alive
			WrappingOptionPtr WrapOpt;					//!< The wrapping options
			MDObjectPtr EssenceDescriptor;				//!< The essence descriptior for the essence as parsed
			UInt32 Stream;								//!< The stream ID of this stream from the parser
			Rational EditRate;							//!< The selected edit rate for this wrapping
			WrappingConfigList SubStreams;				//!< A list of wrapping options available for sub-streams extracted from the same essence source. See \ref SubStreamNotes
			Position StartTimecode;						//!< The starting timecode of this essence, if known, or zero
			bool IsExternal;							//!< True if this is actually going to become an external raw-essence stream
			UInt32 KAGSize;								//!< The selected KAGSize for this wrapping

		protected:
			FileHandle File;							//!< The handle of the file used as the source of this essence
			EssenceSourcePtr Source;					//!< The source to use for wrapping this essence

		public:
			//! Get the essence source for this wrapping - building it if required
			EssenceSourcePtr GetSource(void)
			{
				if(!Source)
				{
					if(!FileValid(File)) error("WrappingConfig::GetSource() called without a call to WrappingConfig::SetFile()\n");
					else Source = Parser->GetEssenceSource(File, Stream);
				}
				return Source;
			}

			//! Set the source file
			void SetFile(FileHandle InFile) { File = InFile; }

			//! Set the essence source
			/*! DRAGONS: When called, this wrapping config will take (shared) ownership of the source */
			void SetSource(EssenceSourcePtr &Value) { Source = Value; }

			//! Set the essence source
			/*! DRAGONS: When called, this wrapping config will take (shared) ownership of the source */
			void SetSource(EssenceSource *Value) { Source = Value; }
		};

		//! Produce a list of available wrapping options
		static WrappingConfigList ListWrappingOptions(bool AllowMultiples, FileHandle InFile, ParserDescriptorListPtr PDList, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None);

		//! Produce a list of available wrapping options
		static WrappingConfigList ListWrappingOptions(FileHandle InFile, ParserDescriptorListPtr PDList, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			return ListWrappingOptions(false, InFile, PDList, ForceEditRate, ForceWrap);
		}

		//! Produce a list of available wrapping options
		static WrappingConfigList ListWrappingOptions(bool AllowMultiples, FileHandle InFile, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None);

		//! Produce a list of available wrapping options
		static WrappingConfigList ListWrappingOptions(FileHandle InFile, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			return ListWrappingOptions(false, InFile, ForceEditRate, ForceWrap);
		}

		//! Produce a list of available wrapping options
		static WrappingConfigList ListWrappingOptions(bool AllowMultiples, FileHandle InFile, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational ForceEditRate(0,0);
			return ListWrappingOptions(AllowMultiples, InFile, ForceEditRate, ForceWrap);
		}

		//! Produce a list of available wrapping options
		static WrappingConfigList ListWrappingOptions(FileHandle InFile, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational ForceEditRate(0,0);
			return ListWrappingOptions(false, InFile, ForceEditRate, ForceWrap);
		}

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, ParserDescriptorListPtr PDList, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			return SelectWrappingOption(false, InFile, PDList, ForceEditRate, ForceWrap);
		}

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, ParserDescriptorListPtr PDList, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(false, InFile, PDList, ForceEditRate, ForceWrap);
		}

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(bool AllowMultiples, FileHandle InFile, ParserDescriptorListPtr PDList, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None);

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(bool AllowMultiples, FileHandle InFile, ParserDescriptorListPtr PDList, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(AllowMultiples, InFile, PDList, ForceEditRate, ForceWrap);
		}

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			return SelectWrappingOption(false, InFile, ForceEditRate, ForceWrap);
		}

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(false, InFile, ForceEditRate, ForceWrap);
		}

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(bool AllowMultiples, FileHandle InFile, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None);

		//! Select the best wrapping option
		static WrappingConfigPtr SelectWrappingOption(bool AllowMultiples, FileHandle InFile, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(AllowMultiples, InFile, ForceEditRate, ForceWrap);
		}

		//! Select the specified wrapping options
		static void SelectWrappingOption(EssenceParser::WrappingConfigPtr Config);

		//! Auto select a  wrapping option (using the default edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(InFile, ForceEditRate);
		}

		//! Select a named wrapping option (with a specified edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, std::string WrappingName, Rational ForceEditRate);

		//! Select a named wrapping option (using the default edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, std::string WrappingName)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(InFile, WrappingName, ForceEditRate);
		}

		//! Select from a list of named wrapping options (with a specified edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, std::list<std::string> WrappingNameList, Rational ForceEditRate);

		//! Select a named wrapping option (using the default edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, std::list<std::string> WrappingNameList)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(InFile, WrappingNameList, ForceEditRate);
		}

		//! Select a UL identified wrapping option (with a specified edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, ULPtr &WrappingID, Rational ForceEditRate);

		//! Select a UL identified wrapping option (using the default edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, ULPtr &WrappingID)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(InFile, WrappingID, ForceEditRate);
		}

		//! Select a UL identified wrapping option (with a specified edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, ULList &WrappingIDList, Rational ForceEditRate);

		//! Select a UL identified wrapping option (using the default edit rate)
		static WrappingConfigPtr SelectWrappingOption(FileHandle InFile, ULList &WrappingIDList)
		{
			Rational ForceEditRate(0,0);
			return SelectWrappingOption(InFile, WrappingIDList, ForceEditRate);
		}

	protected:
		//! Take a list of wrapping options and validate them agains a specified edit rate and wrapping type
		/*! All valid options are built into a WrappingConfig object and added to a specified WrappingConfigList,
		*  which may already contain other items.
		*/
		static void ExtractValidWrappingOptions(WrappingConfigList &Ret, FileHandle InFile, EssenceStreamDescriptorPtr &ESDescriptor, WrappingOptionList &WO, Rational &ForceEditRate, WrappingOption::WrapType ForceWrap);


	protected:
		//! Initialise the sub-parser list
		static void Init(void);
	};


	/* Make top-level versions of EssenceParser::WrappingConfig typedefs */

	//! WrappingConfig object at the top level of mxflib
	typedef EssenceParser::WrappingConfig WrappingConfig;

	//! Smart pointer to a WrappingConfig object
	typedef EssenceParser::WrappingConfigPtr WrappingConfigPtr;

	//! Parent to a WrappingConfig object
	typedef EssenceParser::WrappingConfigParent WrappingConfigParent;

	//! List of smart pointers to WrappingConfig objects
	typedef EssenceParser::WrappingConfigList WrappingConfigList;


	//! An essence source for sub-streams that slave from a master stream
	class EssenceSubSource : public EssenceSource
	{
	protected:
		EssenceSourceParent MasterSource;		//!< The EssenceSource for the master source
		WrappingOptionPtr SelectedWrapping;		//!< The selected wrapping options

	public:
		//! Base constructor
		EssenceSubSource(EssenceSource *Master = NULL) : MasterSource(Master) {};

		//! Virtual destructor to allow polymorphism
		virtual ~EssenceSubSource() {};

		//! Set a new master after construction (useful if the master source is not known at construction time)
		void SetMaster(EssenceSource *Master)
		{
			MasterSource = Master;
		}

		//! Get a pointer to the current master source, this may be NULL if it is not yet defined
		EssenceSource *GetMaster(void)
		{
			return MasterSource;
		}

		//! Is all data exhasted?
		/*! \return true if a call to GetEssenceData() will return some valid essence data
		 */
		virtual bool EndOfData(void)
		{
			if(!MasterSource) return true;
			return MasterSource->EndOfData();
		}

		//! Get data to write as padding after all real essence data has been processed
		/*! If more than one stream is being wrapped, they may not all end at the same wrapping-unit.
		 *	When this happens each source that has ended will produce NULL is response to GetEssenceData().
		 *	The default action of the caller would be to write zero-length KLVs in each wrapping unit for each source that has ended.
		 *	If a source supplies an overload for this method, the supplied padding data will be written in wrapping units following the end of essence instead of a zero-length KLV
		 *	DRAGONS: Note that as the returned value is a non-smart pointer, ownership of the buffer stays with the EssenceSource object.
		 *			 The recommended method of operation is to have a member DataChunk (or DataChunkPtr) allocated the first time padding is required, and return the address each call.
		 *			 The destructor must then free the DataChunk, or allow the smart DataChunkPtr to do it automatically
		 */
		virtual DataChunk *GetPadding(void) { return NULL; }

		//! Get the edit rate of this wrapping of the essence
		/*! \note This may not be the same as the original "native" edit rate of the
		 *        essence if this EssenceSource is wrapping to a different edit rate
		 */
		virtual Rational GetEditRate(void)
		{
			if(!MasterSource) return Rational(1,1);
			return MasterSource->GetEditRate();
		}

		//! Determine if this sub-source can slave from a source with the given wrapping configuration, if so build the sub-config
		/*! \return A smart pointer to the new WrappingConfig for this source as a sub-stream of the specified master, or NULL if not a valid combination
		 */
		virtual EssenceParser::WrappingConfigPtr MakeWrappingConfig(WrappingConfigPtr MasterCfg) = 0;

		//! Configure this sub-source to use the specified wrapping options
		virtual void Use(WrappingOptionPtr WrapOpt)
		{
			SelectedWrapping = WrapOpt;
		}

	};
}


namespace mxflib
{
	//! Base class for handlers to receive notification of the next file about to be opened
	class NewFileHandler : public RefCount<NewFileHandler>
	{
	public:
		virtual ~NewFileHandler() {};

		//! Receive notification of a new file about to be opened
		/*! \param FileName - reference to a std::string containing the name of the file about to be opened - <b>may be changed by this function if required</b>
		 */
		virtual void NewFile(std::string &FileName) = 0;
	};

	//! Smart pointer to a NewFileHandler
	typedef SmartPtr<NewFileHandler> NewFileHandlerPtr;

	// Forware declare the file parser
	class FileParser;

	//! Smart pointer to a FileParser
	typedef SmartPtr<FileParser> FileParserPtr;


	//! List-of-files base class for handling a sequential set of files
	class ListOfFiles
	{
	protected:
		NewFileHandlerPtr Handler;				//!< Handler to be informed of new filenames
		std::string RawFileName;				//!< The raw filename given to start the list (excluding any prepended ! or anything from the first & onwards)
		std::string BaseFileName;				//!< Base filename as a printf string
		std::list<std::string> FollowingNames;	//!< Names to be processed next
		bool FileList;							//!< True if this is a multi-file set rather than a single file (or if a range is in use)
		int ListOrigin;							//!< Start number for filename building
		int ListIncrement;						//!< Number to add to ListOrigin for each new file
		int ListNumber;							//!< The number of files in the list or -1 for "end when no more files"
		int ListEnd;							//!< The last file number in the list or -1 for "end when no more files"
		int FileNumber;							//!< The file number to use for the <b>next</b> source file to open
		int FilesRemaining;						//!< The number of files remaining in the list or -1 for "end when no more files"
		bool AtEOF;								//!< True once the last file has hit it's end of file
		std::string CurrentFileName;			//!< The name of the current file (if open)
		bool ExternalEssence;					//!< True if this essence has been flagged to remain external (filename prepended with "!")
		std::string Options;					//!< A list of options to send to the parser

		Position RangeStart;					//!< The requested first edit unit, or -1 if none specified
		Position RangeEnd;						//!< The requested last edit unit, or -1 if using RequestedDuration
		Length RangeDuration;					//!< The requested duration, or -1 if using RequestedEnd

	public:
		//! Construct a ListOfFiles and optionally set a single source filename pattern
		ListOfFiles(std::string FileName = "") : ExternalEssence(false), RangeStart(-1), RangeEnd(-1), RangeDuration(-1)
		{
			AtEOF = false;

			// Set the filename pattern if required
			if(FileName.size())
			{
				ParseFileName(FileName);
			}
			else
			{
				BaseFileName = "";
				FileList = false;
			}
		}

		//! Virtual destructor to allow polymorphism
		virtual ~ListOfFiles() {}

		//! Set a single source filename pattern
		void SetFileName(std::string &FileName)
		{
			FollowingNames.clear();
			ParseFileName(FileName);
		}

		//! Get the raw filename as used by SetFileName (excluding any prepended ! or anything from the first & onwards)
		/*! DRAGONS: This is not a full description of all files in the list */
		std::string GetRawFileName(void) const
		{
			return RawFileName;
		}

		//! Get a copy of the parser options string
		std::string GetOptions(void) const
		{
			return Options;
		}

		//! Add a source filename pattern
		void AddFileName(std::string &FileName)
		{
			if(BaseFileName.empty())
			{
				ParseFileName(FileName);
			}
			else
			{
				FollowingNames.push_back(FileName);
			}
		}

		//! Set a handler to receive notification of all file open actions
		void SetNewFileHandler(NewFileHandlerPtr &NewHandler) { Handler = NewHandler; }

		//! Set a handler to receive notification of all file open actions
		void SetNewFileHandler(NewFileHandler *NewHandler) { Handler = NewHandler; }

		//! Get the start of any range specified, or -1 if none
		Position GetRangeStart(void) const { return RangeStart; }

		//! Get the end of any range specified, or -1 if none
		Position GetRangeEnd(void) const { return RangeEnd; }

		//! Get the duration of any range specified, or -1 if none
		Position GetRangeDuration(void) const { return RangeDuration; }

		//! Get the current filename
		std::string FileName(void) { return CurrentFileName; }

		//! Open the current file (any new-file handler will already have been called)
		/*! This function must be supplied by the derived class
		 *  \return true if file open succeeded
		 */
		virtual bool OpenFile(void) = 0;

		//! Close the current file
		/*! This function must be supplied by the derived class */
		virtual void CloseFile(void) = 0;

		//! Is the current file open?
		/*! This function must be supplied by the derived class */
		virtual bool IsFileOpen(void) = 0;

		//! Is the current filename pattern a list rather than a single file?
		bool IsFileList(void) { return FileList; }

		//! Open the next file in the set of source files
		/*! \return true if all OK, false if no file or error
		 */
		bool GetNextFile(void);

		//! Has this essence been flagged to remain external (filename prepended with "!")
		bool IsExternal(void) { return ExternalEssence; }

	protected:
		//! Parse a given multi-file name
		void ParseFileName(std::string FileName);

		//! Process an ampersand separated list of sub-file names
		virtual void ProcessSubNames(std::string SubNames) {};
	};


	//! Filter-style source that extracts a range from another EssenceSource
	/*! DRAGONS: This source owns its source, so will keep it alive while we exist
	 *  \note This filter will only work if the original source is configured to produce an edit unit at a time
	 */
	class RangedEssenceSource : public EssenceSource
	{
	protected:
		EssenceSourcePtr Base;								//!< The source being filtered
		Position CurrentPosition;							//!< The current position, stream relative not range relative
		Position RequestedStart;							//!< The requested first edit unit
		Position RequestedEnd;								//!< The requested last edit unit, or -1 if using RequestedDuration
		Length RequestedDuration;							//!< The requested duration, or -1 if using RequestedEnd

		bool Started;										//!< Set true once we have skipped the edit units before any pre-charge
		bool Ending;										//!< Set true once beyond the end of the range, but not necessarily done with the overrun
		bool Ended;											//!< Set true once the overrun is done

		Position PreChargeStart;							//!< The first edit unit in any pre-charge
		DataChunkList PreCharge;							//!< Buffers of pre-charge essence

		DataChunkPtr FirstData;								//!< The first edit unit following any pre-charge (as we will need to read it to check the pre-charge size)

	private:
		//! Prevent a default constructor
		RangedEssenceSource();

		//! Prevent copy construction
		RangedEssenceSource(const RangedEssenceSource &);

	public:
		// Base constructor
		RangedEssenceSource(EssenceSourcePtr Base, Position Start, Position End, Length Duration)
			: EssenceSource(), Base(Base), CurrentPosition(0), RequestedStart(Start), RequestedEnd(End), RequestedDuration(Duration),
		      Started(false), Ending(false), Ended(false), PreChargeStart(-1) {};

		//! Virtual destructor to allow polymorphism
		virtual ~RangedEssenceSource() { };

		//! Get the size of the essence data in bytes
		/*! \note There is intentionally no support for an "unknown" response */
		virtual size_t GetEssenceDataSize(void);

		//! Get the next "installment" of essence data
		/*! This will attempt to return an entire wrapping unit (e.g. a full frame for frame-wrapping) but will return it in
		 *  smaller chunks if this would break the MaxSize limit. If a Size is specified then the chunk returned will end at
		 *  the first wrapping unit end encountered before Size. On no account will portions of two or more different wrapping
		 *  units be returned together. The mechanism for selecting a type of wrapping (e.g. frame, line or clip) is not
		 *  (currently) part of the common EssenceSource interface.
		 *  \return Pointer to a data chunk holding the next data or a NULL pointer when no more remains
		 *	\note If there is more data to come but it is not currently available the return value will be a pointer to an empty data chunk
		 *	\note If Size = 0 the object will decide the size of the chunk to return
		 *	\note On no account will the returned chunk be larger than MaxSize (if MaxSize > 0)
		 */
		virtual DataChunkPtr GetEssenceData(size_t Size = 0, size_t MaxSize = 0);

		//! Did the last call to GetEssenceData() return the end of a wrapping item
		/*! \return true if the last call to GetEssenceData() returned an entire wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the last chunk of a wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the end of a clip-wrapped clip.
		 *  \return false if there is more data pending for the current wrapping unit.
		 *  \return false if the source is to be clip-wrapped and there is more data pending for the clip
		 */
		virtual bool EndOfItem(void) { return Base->EndOfItem(); }

		//! Is all data exhasted?
		/*! \return true if a call to GetEssenceData() will return some valid essence data
		 */
		virtual bool EndOfData(void)
		{
			if(Ended) return true;
			return Base->EndOfData();
		}

		//! Get data to write as padding after all real essence data has been processed
		/*! If more than one stream is being wrapped, they may not all end at the same wrapping-unit.
		 *	When this happens each source that has ended will produce NULL is response to GetEssenceData().
		 *	The default action of the caller would be to write zero-length KLVs in each wrapping unit for each source that has ended.
		 *	If a source supplies an overload for this method, the supplied padding data will be written in wrapping units following the end of essence instead of a zero-length KLV
		 *	DRAGONS: Note that as the returned value is a non-smart pointer, ownership of the buffer stays with the EssenceSource object.
		 *			 The recommended method of operation is to have a member DataChunk (or DataChunkPtr) allocated the first time padding is required, and return the address each call.
		 *			 The destructor must then free the DataChunk, or allow the smart DataChunkPtr to do it automatically
		 */
		virtual DataChunk *GetPadding(void) { return Base->GetPadding(); }

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCEssenceType(void) { return Base->GetGCEssenceType(); }

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCElementType(void) { return Base->GetGCElementType(); }

		//! Is the last data read the start of an edit point?
		virtual bool IsEditPoint(void) { return Base->IsEditPoint(); }

		//! Get the edit rate of this wrapping of the essence
		/*! \note This may not be the same as the original "native" edit rate of the
		 *        essence if this EssenceSource is wrapping to a different edit rate
		 */
		virtual Rational GetEditRate(void) { return Base->GetEditRate(); }

		//! Get the current position in GetEditRate() sized edit units
		/*! This is relative to the start of the stream, so the first edit unit is always 0.
		 *  This is the same as the number of edit units read so far, so when the essence is
		 *  exhausted the value returned shall be the size of the essence
		 */
		virtual Position GetCurrentPosition(void) { return CurrentPosition - RequestedStart; }

		//! Get the preferred BER length size for essence KLVs written from this source, 0 for auto
		virtual int GetBERSize(void) { return Base->GetBERSize(); }

		//! Set a source type or parser specific option
		/*! \return true if the option was successfully set */
		virtual bool SetOption(std::string Option, Int64 Param = 0) { return Base->SetOption(Option, Param); } ;

		//! Get BytesPerEditUnit if Constant, else 0
		/*! \note This value may be useful even if CanIndex() returns false
		 */
		virtual UInt32 GetBytesPerEditUnit(UInt32 KAGSize = 1) { return Base->GetBytesPerEditUnit(KAGSize); }

		//! Can this stream provide indexing
		/*! If true then SetIndex Manager can be used to set the index manager that will receive indexing data
		 */
		virtual bool CanIndex() { return Base->CanIndex(); }

		//! Set the index manager to use for building index tables for this essence
		virtual void SetIndexManager(IndexManagerPtr &Manager, int StreamID)
		{
			Base->SetIndexManager(Manager, StreamID);
		}

		//! Get the index manager
		virtual IndexManagerPtr &GetIndexManager(void) { return Base->GetIndexManager(); }

		//! Get the index manager sub-stream ID
		virtual int GetIndexStreamID(void) { return Base->GetIndexStreamID(); }

		//! Override the default essence key
		virtual void SetKey(DataChunkPtr &Key, bool NonGC = false)
		{
			Base->SetKey(Key, NonGC);
		}

		//! Get the current overridden essence key
		/*! DRAGONS: If the key has not been overridden NULL will be returned - not the default key
			*  \note Defined EssenceSource sub-classes may always use a non-standard key, in which case
			*        they will always return a non-NULL value from this function
			*/
		virtual DataChunkPtr &GetKey(void) { return Base->GetKey(); }

		//! Get true if the default essence key has been overriden with  a key that does not use GC track number mechanism
		/*  \note Defined EssenceSource sub-classes may always use a non-GC-type key, in which case
			*        they will always return true from this function
			*/
		virtual bool GetNonGC(void) { return Base->GetNonGC(); }

		/* Essence type identification */
		/* These functions can be overwridden, or use the base versions to parse GetGCEssenceType() */

		//! Is this picture essence?
		virtual bool IsPictureEssence(void) { return Base->IsPictureEssence(); }

		//! Is this sound essence?
		virtual bool IsSoundEssence(void) { return Base->IsSoundEssence(); }

		//! Is this data essence?
		virtual bool IsDataEssence(void) { return Base->IsDataEssence(); }

		//! Is this compound essence?
		virtual bool IsCompoundEssence(void) { return Base->IsCompoundEssence(); }

		//! An indication of the relative write order to use for this stream
		/*! Normally streams in a GC are ordered as follows:
		 *  - All the CP system items (in Scheme ID then Element ID order)
		 *  - All the GC system items (in Scheme ID then Element ID order)
		 *  - All the CP picture items (in Element ID then Element Number order)
		 *  - All the GC picture items (in Element ID then Element Number order)
		 *  - All the CP sound items (in Element ID then Element Number order)
		 *  - All the GC sound items (in Element ID then Element Number order)
		 *  - All the CP data items (in Element ID then Element Number order)
		 *  - All the GC data items (in Element ID then Element Number order)
		 *  - All the GC compound items (in Element ID then Element Number order) (no GC compound)
		 *
		 *  However, sometimes this order needs to be overridden - such as for VBI data preceding picture items.
		 *
		 *  The normal case for ordering of an essence stream is for RelativeWriteOrder to return 0,
		 *  indicating that the default ordering is to be used. Any other value indicates that relative
		 *  ordering is required, and this is used as the Position value for a SetRelativeWriteOrder()
		 *  call. The value of Type for that call is acquired from RelativeWriteOrderType()
		 *
		 * For example: to force a source to be written between the last GC sound item and the first CP data
		 *              item, RelativeWriteOrder() can return any -ve number, with RelativeWriteOrderType()
		 *				returning 0x07 (meaning before CP data). Alternatively RelativeWriteOrder() could
		 *				return a +ve number and RelativeWriteOrderType() return 0x16 (meaning after GC sound)
		 */
		virtual Int32 RelativeWriteOrder(void) { return Base->RelativeWriteOrder(); }

		//! The type for relative write-order positioning if RelativeWriteOrder() != 0
		/*! This method indicates the essence type to order this data before or after if reletive write-ordering is used
		 */
		virtual int RelativeWriteOrderType(void) { return Base->RelativeWriteOrderType(); }

		//! Get the origin value to use for this essence specifically to take account of pre-charge
		/*! \return Zero if not applicable for this source
		 */
		virtual Length GetPrechargeSize(void)
		{
			if(!Started) LocateStart();
			return static_cast<Length>(RequestedStart - PreChargeStart);
		}

		//! Get the range start position
		virtual Position GetRangeStart(void) { return RequestedStart; }

		//! Get the range end position
		virtual Position GetRangeEnd(void) { return RequestedEnd; }

		//! Get the range duration
		virtual Length GetRangeDuration(void) { return RequestedDuration; }

		//! Get the name of this essence source (used for error messeges)
		virtual std::string Name(void) { return "RangedEssenceSource based on " + Base->Name(); }

		//! Enable VBR indexing, even in clip-wrap mode, by allowing each edit unit to be returned individually
		/*! As the byte offset part of a VBR index table is constructed at write-time each indexed chunk must be written separately.
		 *  When clip-wrapping, this is not normally the case as larger chunks may be returned for efficiency. Setting this mode
		 *  forces each indexable edit unit to be returned by a fresh GetEssenceData call as if it were being frame-wrapped.
		 *  \return true if this mode is supported by the source, and the mode was set, else false
		 */
		virtual bool EnableVBRIndexMode(void) { return Base->EnableVBRIndexMode(); }

	protected:
		//! Locate the first usable edit unit, and if required set the end edit unit
		virtual void LocateStart(void);

		// Allow our very close relation to get even closer
		friend class RangedEssenceSubSource;
	};


	//! Filter-style source that slaves to a RangedEssenceSource to stop parsing when the range is done
	/*! DRAGONS: This source owns its source, so will keep it alive while we exist
	 *  \note This filter will only work if the original source is configured to produce an edit unit at a time
	 */
	class RangedEssenceSubSource : public EssenceSubSource
	{
	protected:
		EssenceSourcePtr Base;								//!< The source being filtered
		Position RequestedStart;							//!< The requested first edit unit
		Position RequestedEnd;								//!< The requested last edit unit, or -1 if using RequestedDuration
		Length RequestedDuration;							//!< The requested duration, or -1 if using RequestedEnd

		bool Started;										//!< Set true once we are ready to start reading
		bool Ended;											//!< Set true once the range is done

	private:
		//! Prevent a default constructor
		RangedEssenceSubSource();

		//! Prevent copy construction
		RangedEssenceSubSource(const RangedEssenceSource &);

	public:
		// Base constructor
		RangedEssenceSubSource(EssenceSourcePtr Base, Position Start, Position End, Length Duration)
			: Base(Base), RequestedStart(Start), RequestedEnd(End), RequestedDuration(Duration), Started(false), Ended(false) {};

		//! Virtual destructor to allow polymorphism
		virtual ~RangedEssenceSubSource() { };

		//! Get the size of the essence data in bytes
		/*! \note There is intentionally no support for an "unknown" response */
		virtual size_t GetEssenceDataSize(void);

		//! Get the next "installment" of essence data
		/*! This will attempt to return an entire wrapping unit (e.g. a full frame for frame-wrapping) but will return it in
		 *  smaller chunks if this would break the MaxSize limit. If a Size is specified then the chunk returned will end at
		 *  the first wrapping unit end encountered before Size. On no account will portions of two or more different wrapping
		 *  units be returned together. The mechanism for selecting a type of wrapping (e.g. frame, line or clip) is not
		 *  (currently) part of the common EssenceSource interface.
		 *  \return Pointer to a data chunk holding the next data or a NULL pointer when no more remains
		 *	\note If there is more data to come but it is not currently available the return value will be a pointer to an empty data chunk
		 *	\note If Size = 0 the object will decide the size of the chunk to return
		 *	\note On no account will the returned chunk be larger than MaxSize (if MaxSize > 0)
		 */
		virtual DataChunkPtr GetEssenceData(size_t Size = 0, size_t MaxSize = 0);

		//! Did the last call to GetEssenceData() return the end of a wrapping item
		/*! \return true if the last call to GetEssenceData() returned an entire wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the last chunk of a wrapping unit.
		 *  \return true if the last call to GetEssenceData() returned the end of a clip-wrapped clip.
		 *  \return false if there is more data pending for the current wrapping unit.
		 *  \return false if the source is to be clip-wrapped and there is more data pending for the clip
		 */
		virtual bool EndOfItem(void) { return Base->EndOfItem(); }

		//! Is all data exhasted?
		/*! \return true if a call to GetEssenceData() will return some valid essence data
		 */
		virtual bool EndOfData(void)
		{
			if(Ended) return true;
			return Base->EndOfData();
		}

		//! Get data to write as padding after all real essence data has been processed
		/*! If more than one stream is being wrapped, they may not all end at the same wrapping-unit.
		 *	When this happens each source that has ended will produce NULL is response to GetEssenceData().
		 *	The default action of the caller would be to write zero-length KLVs in each wrapping unit for each source that has ended.
		 *	If a source supplies an overload for this method, the supplied padding data will be written in wrapping units following the end of essence instead of a zero-length KLV
		 *	DRAGONS: Note that as the returned value is a non-smart pointer, ownership of the buffer stays with the EssenceSource object.
		 *			 The recommended method of operation is to have a member DataChunk (or DataChunkPtr) allocated the first time padding is required, and return the address each call.
		 *			 The destructor must then free the DataChunk, or allow the smart DataChunkPtr to do it automatically
		 */
		virtual DataChunk *GetPadding(void) { return Base->GetPadding(); }

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCEssenceType(void) { return Base->GetGCEssenceType(); }

		//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
		virtual UInt8 GetGCElementType(void) { return Base->GetGCElementType(); }

		//! Is the last data read the start of an edit point?
		virtual bool IsEditPoint(void) { return Base->IsEditPoint(); }

		//! Get the edit rate of this wrapping of the essence
		/*! \note This may not be the same as the original "native" edit rate of the
		 *        essence if this EssenceSource is wrapping to a different edit rate
		 */
		virtual Rational GetEditRate(void) { return Base->GetEditRate(); }

		//! Get the current position in GetEditRate() sized edit units
		/*! This is relative to the start of the stream, so the first edit unit is always 0.
		 *  This is the same as the number of edit units read so far, so when the essence is
		 *  exhausted the value returned shall be the size of the essence
		 */
		virtual Position GetCurrentPosition(void) { return Base->GetCurrentPosition() - RequestedStart; }

		//! Get the preferred BER length size for essence KLVs written from this source, 0 for auto
		virtual int GetBERSize(void) { return Base->GetBERSize(); }

		//! Set a source type or parser specific option
		/*! \return true if the option was successfully set */
		virtual bool SetOption(std::string Option, Int64 Param = 0) { return Base->SetOption(Option, Param); } ;

		//! Get BytesPerEditUnit if Constant, else 0
		/*! \note This value may be useful even if CanIndex() returns false
		 */
		virtual UInt32 GetBytesPerEditUnit(UInt32 KAGSize = 1) { return Base->GetBytesPerEditUnit(KAGSize); }

		//! Can this stream provide indexing
		/*! If true then SetIndex Manager can be used to set the index manager that will receive indexing data
		 */
		virtual bool CanIndex() { return Base->CanIndex(); }

		//! Set the index manager to use for building index tables for this essence
		virtual void SetIndexManager(IndexManagerPtr &Manager, int StreamID)
		{
			Base->SetIndexManager(Manager, StreamID);
		}

		//! Get the index manager
		virtual IndexManagerPtr &GetIndexManager(void) { return Base->GetIndexManager(); }

		//! Get the index manager sub-stream ID
		virtual int GetIndexStreamID(void) { return Base->GetIndexStreamID(); }

		//! Override the default essence key
		virtual void SetKey(DataChunkPtr &Key, bool NonGC = false)
		{
			Base->SetKey(Key, NonGC);
		}

		//! Get the current overridden essence key
		/*! DRAGONS: If the key has not been overridden NULL will be returned - not the default key
			*  \note Defined EssenceSource sub-classes may always use a non-standard key, in which case
			*        they will always return a non-NULL value from this function
			*/
		virtual DataChunkPtr &GetKey(void) { return Base->GetKey(); }

		//! Get true if the default essence key has been overriden with  a key that does not use GC track number mechanism
		/*  \note Defined EssenceSource sub-classes may always use a non-GC-type key, in which case
			*        they will always return true from this function
			*/
		virtual bool GetNonGC(void) { return Base->GetNonGC(); }

		/* Essence type identification */
		/* These functions can be overwridden, or use the base versions to parse GetGCEssenceType() */

		//! Is this picture essence?
		virtual bool IsPictureEssence(void) { return Base->IsPictureEssence(); }

		//! Is this sound essence?
		virtual bool IsSoundEssence(void) { return Base->IsSoundEssence(); }

		//! Is this data essence?
		virtual bool IsDataEssence(void) { return Base->IsDataEssence(); }

		//! Is this compound essence?
		virtual bool IsCompoundEssence(void) { return Base->IsCompoundEssence(); }

		//! An indication of the relative write order to use for this stream
		/*! Normally streams in a GC are ordered as follows:
		 *  - All the CP system items (in Scheme ID then Element ID order)
		 *  - All the GC system items (in Scheme ID then Element ID order)
		 *  - All the CP picture items (in Element ID then Element Number order)
		 *  - All the GC picture items (in Element ID then Element Number order)
		 *  - All the CP sound items (in Element ID then Element Number order)
		 *  - All the GC sound items (in Element ID then Element Number order)
		 *  - All the CP data items (in Element ID then Element Number order)
		 *  - All the GC data items (in Element ID then Element Number order)
		 *  - All the GC compound items (in Element ID then Element Number order) (no GC compound)
		 *
		 *  However, sometimes this order needs to be overridden - such as for VBI data preceding picture items.
		 *
		 *  The normal case for ordering of an essence stream is for RelativeWriteOrder to return 0,
		 *  indicating that the default ordering is to be used. Any other value indicates that relative
		 *  ordering is required, and this is used as the Position value for a SetRelativeWriteOrder()
		 *  call. The value of Type for that call is acquired from RelativeWriteOrderType()
		 *
		 * For example: to force a source to be written between the last GC sound item and the first CP data
		 *              item, RelativeWriteOrder() can return any -ve number, with RelativeWriteOrderType()
		 *				returning 0x07 (meaning before CP data). Alternatively RelativeWriteOrder() could
		 *				return a +ve number and RelativeWriteOrderType() return 0x16 (meaning after GC sound)
		 */
		virtual Int32 RelativeWriteOrder(void) { return Base->RelativeWriteOrder(); }

		//! The type for relative write-order positioning if RelativeWriteOrder() != 0
		/*! This method indicates the essence type to order this data before or after if reletive write-ordering is used
		 */
		virtual int RelativeWriteOrderType(void) { return Base->RelativeWriteOrderType(); }

		//! Get the origin value to use for this essence specifically to take account of pre-charge
		/*! \return Zero if not applicable for this source
		 */
		virtual Length GetPrechargeSize(void) { return 0; }

		//! Get the range start position
		virtual Position GetRangeStart(void) { return RequestedStart; }

		//! Get the range end position
		virtual Position GetRangeEnd(void) { return RequestedEnd; }

		//! Get the range duration
		virtual Length GetRangeDuration(void) { return RequestedDuration; }

		//! Get the name of this essence source (used for error messeges)
		virtual std::string Name(void) { return "RangedEssenceSubSource based on " + Base->Name(); }

		//! Enable VBR indexing, even in clip-wrap mode, by allowing each edit unit to be returned individually
		/*! As the byte offset part of a VBR index table is constructed at write-time each indexed chunk must be written separately.
		 *  When clip-wrapping, this is not normally the case as larger chunks may be returned for efficiency. Setting this mode
		 *  forces each indexable edit unit to be returned by a fresh GetEssenceData call as if it were being frame-wrapped.
		 *  \return true if this mode is supported by the source, and the mode was set, else false
		 */
		virtual bool EnableVBRIndexMode(void) { return Base->EnableVBRIndexMode(); }

		//! Determine if this sub-source can slave from a source with the given wrapping configuration, if so build the sub-config
		/*! \return A smart pointer to the new WrappingConfig for this source as a sub-stream of the specified master, or NULL if not a valid combination
		 */
		virtual EssenceParser::WrappingConfigPtr MakeWrappingConfig(WrappingConfigPtr MasterCfg)
		{
			EssenceSubSource *SubSource = SmartPtr_Cast(Base, EssenceSubSource);
			if(SubSource) return SubSource->MakeWrappingConfig(MasterCfg);
			return NULL;
		}

	protected:
		//! Locate the first usable edit unit, and if required set the end edit unit
		virtual void LocateStart(void);
	};


	//! File parser - parse essence from a sequential set of files
	class FileParser : public ListOfFiles, public RefCount<FileParser>
	{
	protected:
		bool CurrentFileOpen;					//!< True if we have a file open for processing
		FileHandle CurrentFile;					//!< The current file being processed
		EssenceSubParserPtr SubParser;			//!< The sub-parser selected for parsing this sourceessence
		UInt32 CurrentStream;					//!< The currently selected stream in the source essence
		MDObjectPtr CurrentDescriptor;			//!< Pointer to the essence descriptor for the currently selected stream
		WrappingOptionPtr CurrentWrapping;		//!< The currently selected wrapping options
		EssenceSourceParent SeqSource;			//!< This parser's sequential source - which perversely owns the parser!


		//! The essence descriptor describing this essence (if known) else NULL
		MDObjectPtr EssenceDescriptor;

		DataChunkPtr PendingData;				//!< Any pending data from the main stream held over from a previous file if a sub-stream read caused a change of file

		//! Information about a substream
		struct SubStreamInfo
		{
			UInt32 StreamID;					//!< The ID of this sub-stream
			bool Attached;						//!< True for independant sub-streams attached via AddSubStream(), false for sub-streams extracted by the underlying essence parser
			EssenceSourcePtr Source;			//!< The source for the sub-stream data
		};

		//! A list of sub-stream sources, with associated properties
		typedef std::list<SubStreamInfo> SubStreamList;

		SubStreamList SubStreams;				//!< A list of sub-stream sources


	public:
		//! Construct a FileParser and optionally set a single source filename pattern
		FileParser(std::string FileName = "") : ListOfFiles()
		{
			// Let our sequential source know who we are
			SeqSource = new SequentialEssenceSource(this);

			CurrentFileOpen = false;

			// DRAGONS: We have to get our base class to parse the filename here as doing this in the
			//			initializer list prevents polymorphism so the wrong ProcessSubName() is called
			// DRAGONS: Note also that we must do this after SeqSource is added as it is required to add sub-sources
			ParseFileName(FileName);
		}

		//! Clean up when we are destroyed
		~FileParser()
		{
			if(CurrentFileOpen && FileValid(CurrentFile)) FileClose(CurrentFile);
		}


		//! Identify the essence type in the first file in the set of possible files
		ParserDescriptorListPtr IdentifyEssence(void);

		//! Produce a list of available wrapping options
		EssenceParser::WrappingConfigList ListWrappingOptions(bool AllowMultiples, ParserDescriptorListPtr PDList, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational Zero(0,0);
			return ListWrappingOptions(AllowMultiples, PDList, Zero, ForceWrap);
		}

		//! Produce a list of available wrapping options
		EssenceParser::WrappingConfigList ListWrappingOptions(ParserDescriptorListPtr PDList, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational Zero(0,0);
			return ListWrappingOptions(false, PDList, Zero, ForceWrap);
		}

		//! Produce a list of available wrapping options
		EssenceParser::WrappingConfigList ListWrappingOptions(bool AllowMultiples, ParserDescriptorListPtr PDList, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None);

		//! Produce a list of available wrapping options
		EssenceParser::WrappingConfigList ListWrappingOptions(ParserDescriptorListPtr PDList, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			return ListWrappingOptions(false, PDList, ForceEditRate, ForceWrap);
		}

		//! Select the best wrapping option without a forced edit rate
		EssenceParser::WrappingConfigPtr SelectWrappingOption(ParserDescriptorListPtr PDList, UInt32 KAGSize, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational Zero(0,0);
			return SelectWrappingOption(false, PDList, Zero, KAGSize, ForceWrap);
		}

		//! Select the best wrapping option with a forced edit rate
		EssenceParser::WrappingConfigPtr SelectWrappingOption(ParserDescriptorListPtr PDList, Rational ForceEditRate, UInt32 KAGSize = 1, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			return SelectWrappingOption(false, PDList, ForceEditRate, KAGSize, ForceWrap);
		}

		//! Select the best wrapping option without a forced edit rate
		EssenceParser::WrappingConfigPtr SelectWrappingOption(bool AllowMultiples, ParserDescriptorListPtr PDList, UInt32 KAGSize, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			Rational Zero(0,0);
			return SelectWrappingOption(AllowMultiples, PDList, Zero, KAGSize, ForceWrap);
		}

		//! Select the best wrapping option with a forced edit rate
		EssenceParser::WrappingConfigPtr SelectWrappingOption(bool AllowMultiples, ParserDescriptorListPtr PDList, Rational ForceEditRate, WrappingOption::WrapType ForceWrap = WrappingOption::None)
		{
			return SelectWrappingOption(AllowMultiples, PDList, ForceEditRate, 1, ForceWrap);
		}

		//! Select the best wrapping option with a forced edit rate
		EssenceParser::WrappingConfigPtr SelectWrappingOption(bool AllowMultiples, ParserDescriptorListPtr PDList, Rational ForceEditRate, UInt32 KAGSize, WrappingOption::WrapType ForceWrap = WrappingOption::None);

		//! Select the specified wrapping options
		void SelectWrappingOption(EssenceParser::WrappingConfigPtr Config);

		//! Set a wrapping option for this essence
		/*! IdentifyEssence() and IdentifyWrappingOptions() must have been called first
		 */
		void Use(UInt32 Stream, WrappingOptionPtr &UseWrapping);

		//! Set a non-native edit rate
		/*! \return true if this rate is acceptable */
		virtual bool SetEditRate(Rational EditRate);

		//! Return the sequential EssenceSource for the main stream (already aquired internally, so no need to use the stream ID)
		EssenceSourcePtr GetEssenceSource(UInt32 Stream);

		//! Build an EssenceSource to read from the specified sub-stream
		EssenceSourcePtr GetSubSource(UInt32 Stream);

		//! Open the current file (any new-file handler will already have been called)
		/*! Required for ListOfFiles
		 *  \return true if file open succeeded
		 */
		bool OpenFile(void)
		{
			CurrentFile = FileOpenRead(CurrentFileName.c_str());
			CurrentFileOpen = FileValid(CurrentFile);

			return CurrentFileOpen;
		}

		//! Close the current file
		/*! Required for ListOfFiles */
		void CloseFile(void)
		{
			if(CurrentFileOpen) FileClose(CurrentFile);
			CurrentFileOpen = false;
		}

		//! Is the current file open?
		/*! Required for ListOfFiles */
		bool IsFileOpen(void) { return CurrentFileOpen; }

		//! Add a sub-source that will be processed as if it contains data extracted from the primary source
		UInt32 AddSubSource(EssenceSubSource *SubSource);

		//! Set the essence descriptor
		virtual void SetDescriptor(MDObjectPtr Descriptor)
		{
			EssenceDescriptor = Descriptor;
			if(SeqSource) SeqSource->SetDescriptor(Descriptor);
		}

		//! Get a pointer to the essence descriptor for this source (if known) otherwise NULL
		virtual MDObjectPtr GetDescriptor(void) { return EssenceDescriptor; }

	protected:
		//! Set the sequential source to use the EssenceSource from the currently open and identified source file
		/*! \return true if all OK, false if no EssenceSource available
		 */
		bool GetFirstSource(void);

		//! Set the sequential source to use an EssenceSource from the next available source file
		/*! \return true if all OK, false if no EssenceSource available
		 */
		bool GetNextSource(void);

		//! Essence Source that manages a sequence of essence sources from a list of file patterns
		class SequentialEssenceSource : public EssenceSource
		{
		protected:
			EssenceSourcePtr CurrentSource;				//!< An EssenceSource for the current source file
			FileParserPtr Outer;						//!< The outer file parser which is owned by us to prevent it being released until be are done
			Length PreviousLength;						//!< The total size of all previously read essence sources for this set
			bool VBRIndexMode;							//!< Are we needing to use VBRIndexMode for this essence?

			//! Option pair for OptionList
			typedef std::pair<std::string, Int64> OptionPair;

			//! List of all options set for this source
			std::list<OptionPair> OptionList;

			//! Prevent default construction
			SequentialEssenceSource();

		public:
			//! Construct a SequentialEssenceSource
			SequentialEssenceSource(FileParser *Outer) : Outer(Outer), PreviousLength(0), VBRIndexMode(false) {}

			//! Set the new source to use
			void SetSource(EssenceSourcePtr NewSource)
			{
				if(Outer->GetRangeStart() == -1)
				{
					CurrentSource = NewSource;
				}
				else
				{
					/* Process ranged source */
					CurrentSource = new RangedEssenceSource(NewSource, Outer->GetRangeStart(), Outer->GetRangeEnd(), Outer->GetRangeDuration());
				}

				// Set all options
				std::list<OptionPair>::iterator it = OptionList.begin();
				while(it != OptionList.end())
				{
					NewSource->SetOption((*it).first, (*it).second);
					it++;
				}

				// Set the index manager
				if(IndexMan) NewSource->SetIndexManager(IndexMan, IndexStreamID);
			}

			//! Get the size of the essence data in bytes
			virtual size_t GetEssenceDataSize(void)
			{
				if(!ValidSource()) return 0;

				// If we have emptied all files then exit now
				if(Outer->AtEOF) return 0;

				size_t Ret = CurrentSource->GetEssenceDataSize();

				// If no more data move to the next source file
				if(!Ret)
				{
					// Work out how much was read from this file
					Length CurrentSize = (Length)CurrentSource->GetCurrentPosition();

					if(Outer->GetNextSource())
					{
						// Add this length to the previous lengths
						PreviousLength += CurrentSize;

						return GetEssenceDataSize();
					}
				}

				// Return the source size
				return Ret;
			}

			//! Get the next "installment" of essence data
			virtual DataChunkPtr GetEssenceData(size_t Size = 0, size_t MaxSize = 0);

			//! Did the last call to GetEssenceData() return the end of a wrapping item
			virtual bool EndOfItem(void) { if(ValidSource()) return CurrentSource->EndOfItem(); else return true; }

			//! Is all data exhasted?
			virtual bool EndOfData(void) { if(ValidSource()) return CurrentSource->EndOfData(); else return true; }

			//! Get data to write as padding after all real essence data has been processed
			/*! If more than one stream is being wrapped, they may not all end at the same wrapping-unit.
			 *	When this happens each source that has ended will produce NULL is response to GetEssenceData().
			 *	The default action of the caller would be to write zero-length KLVs in each wrapping unit for each source that has ended.
			 *	If a source supplies an overload for this method, the supplied padding data will be written in wrapping units following the end of essence instead of a zero-length KLV
			 *	DRAGONS: Note that as the returned value is a non-smart pointer, ownership of the buffer stays with the EssenceSource object.
			 *			 The recommended method of operation is to have a member DataChunk (or DataChunkPtr) allocated the first time padding is required, and return the address each call.
			 *			 The destructor must then free the DataChunk, or allow the smart DataChunkPtr to do it automatically
			 */
			virtual DataChunk *GetPadding(void)
			{
				if(ValidSource()) return CurrentSource->GetPadding(); else return NULL;
			}

			//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
			virtual UInt8 GetGCEssenceType(void) { if(ValidSource()) return CurrentSource->GetGCEssenceType(); else return 0; }

			//! Get the GCEssenceType to use when wrapping this essence in a Generic Container
			virtual UInt8 GetGCElementType(void) { if(ValidSource()) return CurrentSource->GetGCElementType(); else return 0; }

			//! Is the last data read the start of an edit point?
			virtual bool IsEditPoint(void) { if(ValidSource()) return CurrentSource->IsEditPoint(); else return true; }

			//! Get the edit rate of this wrapping of the essence
			virtual Rational GetEditRate(void) { if(ValidSource()) return CurrentSource->GetEditRate(); else return Rational(0,0); }

			//! Get the current position in GetEditRate() sized edit units
			virtual Position GetCurrentPosition(void)
			{
				if(!ValidSource()) return 0;

				return CurrentSource->GetCurrentPosition() + (Position)PreviousLength;
			}

			//! Get the preferred BER length size for essence KLVs written from this source, 0 for auto
			virtual int GetBERSize(void)
			{
				if(!ValidSource()) return 0;

				return CurrentSource->GetBERSize();
			}

			//! Set a source type or parser specific option
			virtual bool SetOption(std::string Option, Int64 Param = 0)
			{
				if(!ValidSource()) return false;

				// Record this option to allow us to reconfigure sources if we switch source
				OptionList.push_back(OptionPair(Option, Param));

				return CurrentSource->SetOption(Option, Param);
			}

			//! Get BytesPerEditUnit if Constant, else 0
			virtual UInt32 GetBytesPerEditUnit(UInt32 KAGSize = 1) { if(ValidSource()) return CurrentSource->GetBytesPerEditUnit(KAGSize); else return 0; }

			//! Can this stream provide indexing
			virtual bool CanIndex() { if(ValidSource()) return CurrentSource->CanIndex(); else return false; }

			//! Set the index manager to use for building index tables for this essence
			virtual void SetIndexManager(IndexManagerPtr &Manager, int StreamID)
			{
				IndexMan = Manager;
				IndexStreamID = StreamID;

				if(ValidSource()) CurrentSource->SetIndexManager(Manager, StreamID);
			}

			//! Get the index manager
			virtual IndexManagerPtr &GetIndexManager(void) { return IndexMan; }

			//! Get the index manager sub-stream ID
			virtual int GetIndexStreamID(void) { return IndexStreamID; }

			//! Get the origin value to use for this essence specifically to take account of pre-charge
			/*! \return Zero if not applicable for this source
			*/
			virtual Length GetPrechargeSize(void)
			{
				if(!ValidSource()) return 0;

				return CurrentSource->GetPrechargeSize();
			}

			//! Get the range start position
			virtual Position GetRangeStart(void) { return Outer->GetRangeStart(); }

			//! Get the range end position
			virtual Position GetRangeEnd(void) { return Outer->GetRangeEnd(); }

			//! Get the range duration
			virtual Length GetRangeDuration(void) { return Outer->GetRangeDuration(); }

			//! Get the name of this essence source (used for error messeges)
			virtual std::string Name(void) { if(ValidSource()) return "SequentialEssenceSource based on " + CurrentSource->Name(); else return "SequentialEssenceSource"; }

			//! Enable VBR indexing, even in clip-wrap mode, by allowing each edit unit to be returned individually
			/*! As the byte offset part of a VBR index table is constructed at write-time each indexed chunk must be written separately.
			*  When clip-wrapping, this is not normally the case as larger chunks may be returned for efficiency. Setting this mode
			*  forces each indexable edit unit to be returned by a fresh GetEssenceData call as if it were being frame-wrapped.
			*  \return true if this mode is supported by the source, and the mode was set, else false
			*/
			virtual bool EnableVBRIndexMode(void)
			{
				// Set the mode and record a flag to redo this each new file (if it succeeded)
				if(ValidSource()) return VBRIndexMode = CurrentSource->EnableVBRIndexMode(); else return false;
			}

			//! Is this source a system item rather than an essence source?
			virtual bool IsSystemItem(void) { if(ValidSource()) return CurrentSource->IsSystemItem(); else return false; }

			//! Is this source a generic stream item rather than an normal essence source?
			virtual bool IsGStreamItem(void) { if(ValidSource()) return CurrentSource->IsGStreamItem(); else return false; }

			//! Attach a related System Item source to the owning BodyStream if required
			/*! DRAGONS: This is currently a non-ideal fudge - do not assume this method will last long!!! */
			virtual void AttachSystem(BodyStream *Stream)  { if(ValidSource()) CurrentSource->AttachSystem(Stream); }


		protected:
			//! Ensure that CurrentSource is valid and ready for reading - if not select the next source file
			/*! \return true if all OK, false if no EssenceSource available
			 */
			bool ValidSource(void);

			// Allow the parser to access our internals
			friend class FileParser;
		};

		// Allow our protected member to access our internals
		friend class SequentialEssenceSource;

		//! Send options to a sub-parser based on a formatted string
		/*! Each option is a string with an optional equals and Int64 number. Options are semi-colon separated.
		 *  The option string may be within quotes, if not it will be left and right trimmed to remove spaces
		 */
		void SendParserOptions(EssenceSubParserPtr &SubParser, const std::string Options);

		//! Process an ampersand separated list of sub-file names
		virtual void ProcessSubNames(std::string SubNames);
	};
}



// GCReader and associated structures
namespace mxflib
{
	//! Content Units to be counted to evaluate when to stop ReadFromFile
	typedef enum _ReaderUnit { Unit_KLV, Unit_GC, Unit_Part, Unit_Cont } ReaderUnit;

	//! Base class for GCReader handlers
	/*! \note Classes derived from this class <b>must not</b> include their own RefCount<> derivation
	 */
	class GCReadHandler_Base : public RefCount<GCReadHandler_Base>
	{
	public:
		//! Base destructor
		virtual ~GCReadHandler_Base() {};

		//! Handle a "chunk" of data that has been read from the file
		/*! \return true if all OK, false on error
		 */
		virtual bool HandleData(GCReaderPtr Caller, KLVObjectPtr Object) = 0;
	};

	// Smart pointer for the base GCReader read handler
	typedef SmartPtr<GCReadHandler_Base> GCReadHandlerPtr;

	//! Class that reads data from an MXF file
	class GCReader : public RefCount<GCReader>
	{
	protected:
		MXFFilePtr File;								//!< File from which to read
		Position FileOffset;							//!< The offset of the start of the current (or next) KLV within the file. Current KLV during HandleData() and next at other times.
		Position StreamOffset;							//!< The offset of the start of the current KLV within the data stream

		bool StopNow;									//!< True if no more KLVs should be read - set by StopReading() and ReadFromFile() when Focus,Unit,Count is satisfied
		bool StopCalled;								//!< True if StopReading() called while processing the current KLV
		bool PushBackRequested;							//!< True if StopReading() called with PushBackKLV = true

		GCReadHandlerPtr DefaultHandler;				//!< The default handler to receive all KLVs without a specific handler
		GCReadHandlerPtr FillerHandler;					//!< The hanlder to receive all filler KLVs
		GCReadHandlerPtr EncryptionHandler;				//!< The hanlder to receive all encrypted KLVs

		std::map<UInt32, GCReadHandlerPtr> Handlers;	//!< Map of read handlers indexed by track number

	public:
		//! Create a new GCReader, optionally with a given default item handler and filler handler
		/*! \note The default handler receives all KLVs without a specific handler (except fillers)
		 *        The filler handler receives all filler KLVs
		 */
		GCReader( MXFFilePtr File, GCReadHandlerPtr DefaultHandler = NULL, GCReadHandlerPtr FillerHandler = NULL );

		//! Set the default read handler
		/*! This handler receives all KLVs without a specific data handler assigned
		 *  including KLVs that do not appear to be standard GC KLVs.  If not default handler
		 *  is set KLVs with no specific handler will be discarded.
		 */
		void SetDefaultHandler(GCReadHandlerPtr DefaultHandler = NULL)
		{
			this->DefaultHandler = DefaultHandler;
		}

		//! Set the filler handler
		/*! If no filler handler is set all filler KLVs are discarded
		 *  \note Filler KLVs are <b>never</b> sent to the default handler
		 *        unless it is also set as the filler handler
		 */
		void SetFillerHandler(GCReadHandlerPtr FillerHandler = NULL)
		{
			this->FillerHandler = FillerHandler;
		}

		//! Set encryption handler
		/*! This handler will receive all encrypted KLVs and after decrypting them will
		 *  resubmit the decrypted version for handling using function HandleData()
		 */
		void SetEncryptionHandler(GCReadHandlerPtr EncryptionHandler = NULL)
		{
			this->EncryptionHandler = EncryptionHandler;
		}

		//! Set data handler for a given track number
		void SetDataHandler(UInt32 TrackNumber, GCReadHandlerPtr DataHandler = NULL)
		{
			if(DataHandler)
			{
				Handlers[TrackNumber] = DataHandler;
			}
			else
			{
				Handlers.erase(TrackNumber);
			}
		}

		//! Read from file - and specify a start location
		/*! All KLVs are dispatched to handlers
		 *  Stops reading when Focus,Unit,Count is satisfied (default=false,Unit_KLV,1)
		 *  \param FilePos Location within the file to start this read
		 *  \param StreamPos Stream offset of the first KLV to be read
		 *  \param Focus (default false) if reading will Stop after the Unit and Count params are satisfied
		 *	\param Unit enum (default Unit_KLV) of what Unit of content {Unit_KLV,Unit_GC,Unit_Part,Unit_Cont} is to be Counted
		 *	\param Count count (default 1) of how many Units are be read prior to stop (0==forever)
		 *  \return true if all went well, false end-of-file, an error occured or StopReading() was called
		 */
		bool ReadFromFile(Position FilePos, Position StreamPos, bool Focus = false, ReaderUnit Unit = Unit_KLV, int Count = 1 )
		{
			FileOffset = FilePos;					// Record the file location
			StreamOffset = StreamPos;				// Record the stream location

			return ReadFromFile(Focus,Unit,Count);			// Then do a "continue" read
		}

		//! Read from file - continuing from a previous read
		/*! All KLVs are dispatched to handlers
		 *  Stops reading when Focus,Unit,Count is satisfied (default=false,Unit_KLV,1)
		 *  \return true if all went well, false end-of-file, an error occured or StopReading() was called
		 */
		bool ReadFromFile(bool Focus = false, ReaderUnit Unit = Unit_KLV, int Count = 1 );

		//! Set the offset of the start of the next KLV in the file
		/*! Generally this will only be called as a result of parsing a partition pack
		 *  \note The offset will increment automatically as data is read.
		 *        If a seek is performed the offset will need to be adjusted.
		 */
		void SetFileOffset(Position NewOffset)
		{
			FileOffset = NewOffset;
		};

		//! Set the offset of the start of the next KLV within this GC stream
		/*! Generally this will only be called as a result of parsing a partition pack
		 *  \note The offset will start at zero and increment automatically as data is read.
		 *        If a seek is performed the offset will need to be adjusted.
		 */
		void SetStreamOffset(Position NewOffset)
		{
			StreamOffset = NewOffset;
		};

		//! Get the file offset of the next read (or the current KLV if inside ReadFromFile)
		/*! \note This is not the correct way to access the raw KLV in the file - that should be done via the KLVObject.
		 *        This function allows the caller to determine where the file pointer ended up after a read.
		 */
		Position GetFileOffset(void) { return FileOffset; }


		/*** Functions for use by read handlers ***/

		//! Force a KLVObject to be handled
		/*! \note This is not the normal way that the GCReader is used, but allows the encryption handler
		 *        to push the decrypted data back to the GCReader to pass to the appropriate handler
		 *  \return true if all OK, false on error
		 */
		bool HandleData(KLVObjectPtr Object);

		//! Stop reading even though there appears to be valid data remaining
		/*! This function can be called from a handler if it detects that the current KLV is either the last
		 *  KLV in partition, or does not belong in this partition at all.  If the KLV belongs to another
		 *  partition, or handling should be deferred for some reason, PushBackKLV can be set to true
		 */
		void StopReading(bool PushBackKLV = false);

		//! Get the offset of the start of the current KLV within this GC stream
		Position GetStreamOffset(void) { return StreamOffset; };
	};
}


// BodyReader and associated structures
namespace mxflib
{
	//! BodyReader class - reads from an MXF file (reads data is "pulled" from the file)
	class BodyReader : public RefCount<BodyReader>
	{
	protected:
		MXFFilePtr File;						//!< File from which to read
		Position CurrentPos;					//!< Current position within file

		bool NewPos;							//!< The value of CurrentPos has been updated by a seek - therefore reading must be reinitialized!
		bool SeekInited;						//!< True once the per SID seek system has been initialized
		bool AtPartition;						//!< Are we (to our knowledge) at the start of a partition pack?
		bool AtEOF;								//!< Are we (to our knowledge) at the end of the file?

		UInt32 CurrentBodySID;					//!< The currentBodySID being processed

		GCReadHandlerPtr GCRDefaultHandler;		//!< Default handler to use for new GCReaders
		GCReadHandlerPtr GCRFillerHandler;		//!< Filler handler to use for new GCReaders
		GCReadHandlerPtr GCREncryptionHandler;	//!< Encryption handler to use for new GCReaders

		std::map<UInt32, GCReaderPtr> Readers;	//!< Map of GCReaders indexed by BodySID

	public:
		//! Construct a body reader and associate it with an MXF file
		BodyReader(MXFFilePtr File);

		//! Seek to a specific point in the file
		/*! \return New location or -1 on seek error
		 */
		Position Seek(Position Pos = 0);

		//! Tell the current file location
		Position Tell(void) { return CurrentPos; };

		//! Seek to a specific byte offset in a given stream
		/*! \return New file offset or -1 on seek error
		 */
		Position Seek(UInt32 BodySID, Position Pos);

		//! Report the byte offset in a given stream
		/*! \return File offset or -1 if not known or stream does not exist
		 */
		Position Tell(UInt32 BodySID);

		//! Set the default handler for all new GCReaders
		/*! Each time a new GCReader is created this default handler will be used if no other is specified
		 */
		void SetDefaultHandler(GCReadHandlerPtr DefaultHandler = NULL) { GCRDefaultHandler = DefaultHandler; };

		//! Set the filler handler for all new GCReaders
		/*! Each time a new GCReader is created this filler handler will be used if no other is specified
		 */
		void SetFillerHandler(GCReadHandlerPtr FillerHandler = NULL) { GCRFillerHandler = FillerHandler; };

		//! Set the encryption handler for all new GCReaders
		/*! Each time a new GCReader is created this encryption handler will be used
		 */
		void SetEncryptionHandler(GCReadHandlerPtr EncryptionHandler = NULL) { GCREncryptionHandler = EncryptionHandler; };

		//! Make a GCReader for the specified BodySID
		/*! \return true on success, false on error (such as there is already a GCReader for this BodySID)
		 */
		bool MakeGCReader(UInt32 BodySID, GCReadHandlerPtr DefaultHandler = NULL, GCReadHandlerPtr FillerHandler = NULL);

		//! Make a GCReader for the specified BodySID
		/*! \return A pointer to the new Reader, or the NULL on error (such as there is already a GCReader for this BodySID)
		 */
		GCReader* NewGCReader(UInt32 BodySID, GCReadHandlerPtr DefaultHandler = NULL, GCReadHandlerPtr FillerHandler = NULL);

		//! Get a pointer to the GCReader used for the specified BodySID
		GCReaderPtr GetGCReader(UInt32 BodySID)
		{
			// See if we have a GCReader for this BodySID
			std::map<UInt32, GCReaderPtr>::iterator it = Readers.find(BodySID);

			// If not found return NULL
			if(it == Readers.end()) return NULL;

			// Return the pointer
			return (*it).second;
		}

		//! Read from file
		/*! All KLVs are dispatched to handlers
		 *  Stops reading when Focus,Unit,Count is satisfied (default=false,Unit_KLV,1)
		 *  \return true if all went well, false end-of-file, an error occured or StopReading()
		 *          was called on the current GCReader
		 */
		bool ReadFromFile(bool Focus = false, ReaderUnit Unit = Unit_KLV, int Count= 1 );

		//! Resync after possible loss or corruption of body data
		/*! Searches for the next partition pack and moves file pointer to that point
		 *  \return false if an error (or EOF found)
		 */
		bool ReSync();

		//! Are we currently at the start of a partition pack?
		bool IsAtPartition(void);

		//! Are we currently at the end of the file?
		bool Eof(void);


		/*** Functions for use by read handlers ***/

		//! Get the BodySID of the current location (0 if not known)
		UInt32 GetBodySID(void) { return CurrentBodySID; }


	protected:
		//! Initialize the per SID seek system
		/*! To allow us to seek to byte offsets within a file we need to initialize
		 *  various structures - seeking is not always possible!!
		 *  \return False if seeking could not be initialized (perhaps because the file is not seekable)
		 */
		bool InitSeek(void);
	};

	//! Smart pointer to a BodyReader
	typedef SmartPtr<BodyReader> BodyReaderPtr;
}


//*********************************************
//**                                         **
//**        General essence functions        **
//**                                         **
//*********************************************

namespace mxflib
{
	//! Structure to hold information about each stream in a GC
	struct GCElementKind
	{
		bool	IsValid;					//!< true if this is a GC Element
		UInt8	Item;						//!< Item type - byte 13
		UInt8	Count;						//!< Element count - byte 14
		UInt8	ElementType;				//!< Element type - byte 15
		UInt8	Number;						//!< Element number - byte 16

		bool operator==( const GCElementKind R ) const
		{
			if( !IsValid && !R.IsValid ) return true;
			return (Item==R.Item && Count==R.Count && ElementType==R.ElementType && Number==R.Number);
		}

		bool operator!=( const GCElementKind R ) const { return !operator==(R); }
	};

	//! Register an essence key to be treated as a GC essence key
	/*! This allows private or experimental essence keys to be treated as standard GC keys when reading
	 *  /note If the size is less than 16-bytes, only that part of the key given will be compared (all the rest will be treated as wildcard bytes).
	 *        Byte 8 is never compared (the version number byte)
	 */
	void RegisterGCElementKey(DataChunkPtr &Key);

	//! Register a system item key to be treated as a GC system key
	/*! This allows private or experimental essence keys to be treated as standard GC keys when reading
	 *  /note If the size is less than 16-bytes, only that part of the key given will be compared (all the rest will be treated as wildcard bytes).
	 *        Byte 8 is never compared (the version number byte)
	 */
	void RegisterGCSystemKey(DataChunkPtr &Key);

	//! Get a GCElementKind structure from a key
	GCElementKind GetGCElementKind(const ULPtr TheUL);

	//! Determine if this is a system item
	bool IsGCSystemItem(const ULPtr TheUL);

	//! Determine if this is a generic stream item
	bool IsGStreamItem(const ULPtr TheUL);

	//! Get the track number of this essence key (if it is a GC Key)
	/*! \return 0 if not a valid GC Key
	 */
	UInt32 GetGCTrackNumber(ULPtr TheUL);

	//! GCLayout class maintains a vector of GC elements (both Sys and Essence) discovered while reading
	class GCLayout
	{
	private:
		std::vector<GCElementKind>	_current;	//!< pre-built or pre-loaded layour, used to compare against
		std::vector<GCElementKind>	_fresh;		//!< layout building now whilst comparing

		bool	_valid;							//!< validity of _current. false = don't compare
		bool	_inconsistent;					//!< true = inconsistency found in _fresh relative to _current
		bool	_autorefresh;					//!< true = automatically replace _current at start of next GC, see below

		//! normal behaviour is to save _fresh as _current upon starting new GC (i.e. when Offer returns 3)

		Position _pos;							//!< count how many complete GCs

	public:
		GCLayout( const bool refresh = true )
		{
			_valid = false;
			_inconsistent = false;
			_autorefresh = refresh;

			_pos = 0;

			_current.reserve( 16 );				// to avoid unnecessary reallocation
			_fresh.reserve( 16 );
		};

		~GCLayout()
		{
			_current.clear();
			_fresh.clear();
		};

		bool IsValid() { return _valid; }
		bool IsConsistent() { return !_inconsistent; }

		//! return how many since last reset
		Position Pos() { return _pos; }

		//! most recent element
		GCElementKind Which() { return _fresh.back(); }

		size_t Size() { if( _valid ) return _current.size(); else return _fresh.size(); }

		//! reset _current, Where() and set _autorefresh
		void Reset( const bool refresh = true )
		{
			_valid = false;
			_inconsistent = false;
			_autorefresh = refresh;

			_pos = 0;

			_current.clear();
			_fresh.clear();
		}

		//! Offer an element and report status
			// -1 = inconsistent
			//	0 = added OK
			//	1 = added OK, presume next will be last
			//	2 = added OK, presumed was last
			//	3 = starts new GC	note: may also be inconsistent
		int Offer( const GCElementKind me )
		{
			if( !_valid )
			{
				if( _fresh.empty() )										// first element
				{
					_fresh.push_back( me ); return 3;						// ok
				}
				else if( _fresh.size()==1 && me==_fresh.back()				// double element
					 ||( _fresh.size()>1 && me==_fresh.front() ) )			// repeat cycle
				{
					if( _autorefresh ) { _current = _fresh;	_valid = true; _inconsistent = false; }
					_fresh.clear();	_fresh.push_back( me );	_pos++; return 3;		// start new
				}
				else if( (me.Item&0xF) > 4 && (_fresh.back().Item&0xF) == 7	// Pic, Snd, Cpd allowed after Data
				     ||( (me.Item&0xF) >= (_fresh.back().Item&0xF)      ) )	// otherwise allowed in seq Sys, Pic, Snd, Data, Cpd
				{
					_fresh.push_back( me ); return 0;						// ok
				}
				else
				{
					if( _autorefresh ) { _current = _fresh;	_valid = true; _inconsistent = false; }
					_fresh.clear();	_fresh.push_back( me );	_pos++; return 3;		// start new
				}
			}
			else
			{
				if( _current.size() >= 1 && me == _current.front() )
				{
					_fresh.clear();	_fresh.push_back( me );	_pos++; return 3;		// start new
				}

				_fresh.push_back( me );

				if( _fresh.size() > _current.size() ) { _inconsistent = true; return -1; }			// just became inconsistently bigger

				if( _fresh.size() == _current.size() )
				{
					if( me != _current.back() ) { _inconsistent = true; return -1; } else return 2; // was last
				}
				else if( _fresh.size() == (_current.size()-1) )
				{
					if( me != _current.back() ) { _inconsistent = true; return -1; } else return 1; // was penultimate
				}

				if( me != _current.back() ) { _inconsistent = true; return -1; } else return 0;	// OK
			}
		};

		//! report where in _current
			// -1 = no _curr
			//	0 = in middle
			//	1 = next will be last
			//	2 = at end
			//	3 = just started
		int Where()
		{
			if( !_valid || _current.empty() ) return -1;					// no curr

			if( _fresh.size() == _current.size() ) return 2;				// at end
			else if( _fresh.size() == (_current.size()-1) ) return 1;		// penultimate
			else if( _fresh.size() == 1 ) return 3;							// first

			return 0;														// somewhere
		};

		//! force immediate end of _fresh layout
			// -1 = was not at end
			//  2 = OK
		int	ForceEnd()
		{
			_current = _fresh; _inconsistent = false; _fresh.clear();

			if( Where()!=2 ) return -1;										// wasn't at end
			else { _valid = true; return 2;	}								// ok
		};
	};
}


/* BodyWriter and related classes */

namespace mxflib
{
	//! Class holding data relating to a stream to be written by BodyWriter
	/*! Sub-streams can be added as pointers to their essence sources as this class is derived from EssenceSourceList.
	 *  Sub-streams will be written in the same generic container as this stream.
	 *  This stream's essence source will appear as the first "child" when the EssenceSourceList is scanned
	 */
	class BodyStream : public RefCount<BodyStream>, public EssenceSourceList
	{
	public:
		//! Define the action required next for this stream
		enum StateType
		{
			BodyStreamStart = 0,									//!< This stream has not yet done anything - state unknown
			BodyStreamHeadIndex,									//!< Next action: Write a "header" index table - if required in an isolated partition following the header
			BodyStreamPreBodyIndex,									//!< Next action: Write an isolated index table before the next body partition
			BodyStreamBodyWithIndex,								//!< Next action: Write a body partition with an index table
			BodyStreamBodyNoIndex,									//!< Next action: Write a body partition without index table
			BodyStreamPostBodyIndex,								//!< Next action: Write an isolated index table after a body partition
			BodyStreamFootIndex,									//!< Next action: Write a "footer" index table - if required in an isolated partition before the footer
			BodyStreamDone											//!< All done - no more actions required
		};

		//! The index table type or types of this stream
		enum IndexType
		{
			StreamIndexNone = 0,									//!< No index table will be written
			StreamIndexFullFooter = 1,								//!< A full index table will be written in the footer if possible (or an isolated partition just before the footer if another index is going to use the footer)
			StreamIndexSparseFooter = 2,							//!< A sparse index table will be written in the footer if possible (or an isolated partition just before the footer if another index is going to use the footer)
			StreamIndexSprinkled = 4,								//!< A full index table will be sprinkled through the file, one chunk in each of this essence's body partitions, and one in or just before the footer
			StreamIndexSprinkledIsolated = 8,						//!< A full index table will be sprinkled through the file, one chunk in an isolated partition following each of this essence's body partitions
			StreamIndexCBRHeader = 16,								//!< A CBR index table will be written in the header (or an isolated partition following the header if another index table exists in the header)
			StreamIndexCBRHeaderIsolated = 32,						//!< A CBR index table will be written in an isolated partition following the header
			StreamIndexCBRFooter = 64,								//!< A CBR index table will be written in the footer if possible (or an isolated partition just before the footer if another index is going to use the footer)
			StreamIndexCBRBody = 128,								//!< A CBR index table will be written in each body partition for this stream
			StreamIndexCBRIsolated = 256,							//!< A CBR index table will be written in an isolated body partition following each partition of this stream
			StreamIndexCBRPreIsolated = 512							//!< A CBR index table will be written in an isolated body partition before each partition of this stream
		};

		//! Wrapping types for streams
		enum WrapType
		{
			StreamWrapOther = 0,									//!< Other non-standard wrapping types - the essence source will supply one KLVs worth at a time (??)
			StreamWrapFrame,										//!< Frame wrapping
			StreamWrapClip											//!< Clip wrapping
		};

	protected:
		EssenceSourcePtr Source;									//!< The essence source for this stream
		EssenceSourceList SubStreams;								//!< Sources for each sub-stream
		EssenceSourceList::iterator SubStream_it;					//!< Current sub-stream
		bool SubStreamRestart;										//!< Flag true when the sub-stream iterator needs moving to the top of the list next time

		StateType State;											//!< The state of this stream

		IndexType StreamIndex;										//!< The index type(s) of this stream
		IndexType FooterIndexFlags;									//!< Set of flags for tracking footer index tables

		UInt32 BodySID;												//!< BodySID to use for this stream
		UInt32 IndexSID;											//!< IndexSID to use for indexing this stream

		WrapType StreamWrap;										//!< The wrapping type of this stream

		GCWriter* StreamWriter;										//!< The writer for this stream

		bool EssencePendingData;									//!< Is there any essence data pending in the writer?

		bool EndOfStream;											//!< No more essence available for this stream

		IndexManagerPtr IndexMan;									//!< The index manager for this stream

		Position NextSprinkled;										//!< The location of the first edit-unit to use for the next sprinkled index segment
		size_t PrevSprinkleSize;									//!< The size of the most recent sprinkled index table

		bool FreeSpaceIndex;										//!< True if the free space at the end of the essenc eis to be indexed
																	/*!< When an essence stream may be extended during file creation it may be useful to know
																	 *   where the essence currently ends (to allow new essence to be added).
																	 *   /note DRAGONS: This is non-standard and will produce invalid index tables (even if they are later "fixed")
																	 */

		bool ValueRelativeIndexing;									//!< Flag to allow value-relative indexing
																	/*!< \note This is NOT implemented in the IndexManager, but must be handled by the caller */

		Length PrechargeSize;										//!< The number of edit units of pre-charge remaining to be written
																	/*!< DRAGONS: This is set when the state moves from "start" because it is
																	 *            important to wait for all sub-streams to be set up first
																	 */

		//! The fixed position for this stream, or (0 - 0x7fffffff) if not fixed
		/*! This allows the position to be fixed at the start of processing an edit unit, then unfixed at the end */
		Position FixedPosition;

		Length OverallEssenceSize;									//!< The number of raw essence bytes written from this stream so far
																	/*!< DRAGONS: This is only the raw bytes, not including keys, lengths or any filler
																	 */

		//! KLV Alignment Grid to use for this stream (of zero if default for this body is to be used)
		UInt32 KAG;

		//! Flag set if BER lengths for this stream should be forced to 4-byte (where possible)
		bool ForceBER4;

		//! Flag set if partitioning is to be done only on edit boundaries
		/*! \note Only the master stream is (currently) edit aligned, not all sub-streams */
		bool EditAlign;


		//! Prevent NULL construction
		BodyStream();

		//! Prevent copy construction
		BodyStream(BodyStream &);

	/* Public properties */
	public:
		std::list<Position> SparseList;								//!< List of edit units to include in sparse index tables

	public:
		//! Construct a body stream object with a given essence source
		BodyStream(UInt32 SID, EssenceSourcePtr &EssSource, DataChunkPtr Key = NULL, bool NonGC = false)
		{
			BodySID = SID;
			IndexSID = 0;
			Source = EssSource;
			State = BodyStreamStart;
			StreamIndex = StreamIndexNone;
			FooterIndexFlags = StreamIndexNone;
			StreamWrap = StreamWrapOther;
			StreamWriter = NULL;
			SubStreamRestart = true;
			NextSprinkled = 0;
			PrevSprinkleSize = 0;
			EssencePendingData = false;
			EndOfStream = false;
			FreeSpaceIndex = false;
			ValueRelativeIndexing = false;
			PrechargeSize = 0;
			OverallEssenceSize = 0;

			// Clear the fixed position
			SetFixedPosition();

			KAG = 0;
			ForceBER4 = false;
			EditAlign = false;

			// Set the non-standard key if requested
			if(Key) EssSource->SetKey(Key, NonGC);

			// Set the master stream as one of the essence streams
			push_back(Source);

			// Inform the master stream that we are holding them
			EssSource->SetBodyStream(this);

			// Allow the master stream to attach a system item if required
			EssSource->AttachSystem(this);
		}

		//! Get the essence source for this stream
		EssenceSourcePtr &GetSource(void) { return Source; }

		//! Get the number of sub-streams (includes the master stream)
		size_type SubStreamCount(void) { return size(); }

		//! Add a new sub-stream
		void AddSubStream(EssenceSourcePtr &SubSource, DataChunkPtr Key = NULL, bool NonGC = false);

		//! Get this stream's BodySID
		UInt32 GetBodySID(void) { return BodySID; }

		//! Set this stream's IndexSID
		void SetIndexSID(UInt32 SID) { IndexSID = SID; }

		//! Get this stream's IndexSID
		UInt32 GetIndexSID(void) { return IndexSID; }

		//! Set the stream's state
		void SetState(StateType NewState) { State = NewState; }

		//! Get the current state
		StateType GetState(void)
		{
			if(State == BodyStreamStart) GetNextState();
			return State;
		}

		//! Get the next state
		/*! Sets State to the next state
		 *  \return The next state (now the current state)
		 */
		StateType GetNextState(void);

		//! Add the specified index type(s)
		void AddIndexType(IndexType NewIndexType) { StreamIndex = (IndexType) (StreamIndex | NewIndexType); }

		//! Set the index type(s) to the desired value
		/*! \note This sets the complete value, it doesn't just add an option - to add "X" use AddIndexType()
		 */
		void SetIndexType(IndexType NewIndexType) { StreamIndex = NewIndexType; }

		//! Get the index type(s)
		IndexType GetIndexType(void) { return StreamIndex; }

		//! Set the footer index flags to the desired value
		/*! \note This sets the complete value, it doesn't just add an option - to add "X" use SetFooterIndex(GetFooterIndex() | "X");
		 */
		void SetFooterIndex(IndexType NewIndexType) { FooterIndexFlags = NewIndexType; }

		//! Get the footer index flags
		IndexType GetFooterIndex(void) { return FooterIndexFlags; }

		//! Set the wrapping type for this stream
		void SetWrapType(WrapType NewWrapType) { StreamWrap = NewWrapType; }

		//! Set the wrapping type for this stream
		void SetWrapType(WrappingOption::WrapType NewWrapType)
		{
			if(NewWrapType == WrappingOption::Frame) StreamWrap = StreamWrapFrame;
			else if(NewWrapType == WrappingOption::Clip) StreamWrap = StreamWrapClip;
			else StreamWrap = StreamWrapOther;
		}

		//! Get the wrapping type of this stream
		WrapType GetWrapType(void) { return StreamWrap; }

		//! Set the current GCWriter
		void SetWriter(GCWriter* Writer);

		//! Get the current index manager
		IndexManagerPtr &GetIndexManager(void)
		{
			if(!IndexMan) InitIndexManager();
			return IndexMan;
		}

		//! Get a pointer to the current GCWriter
		GCWriter* GetWriter(void) { return StreamWriter; }

		//! Get the track number associated with this stream
		UInt32 GetTrackNumber(void)
		{
			if(!Source) return 0;
			return StreamWriter->GetTrackNumber(Source->GetStreamID());
		}

		//! Get the track number associated with a specified stream or sub-stream
		Uint32 GetTrackNumber(GCStreamID ID)
		{
			if(!Source) return 0;
			return StreamWriter->GetTrackNumber(ID);
		}

		//! Set the pending essence data flag
		void SetPendingData(bool Value = true) { EssencePendingData = Value; }

		//! Find out if there is any essence data stored in the GCWriter pending a write
		bool HasPendingData(void) { return EssencePendingData; }

		//! Set the EndOfStream flag
		void SetEndOfStream(bool Value = true) { EndOfStream = Value; }

		//! Find out if there is any essence data remaining for this stream
		bool GetEndOfStream(void) { return EndOfStream; }

		//! Set the first edit unit for the next sprinkled index segment
		void SetNextSprinkled(Position Sprinkled) { NextSprinkled = Sprinkled; }

		//! Get the first edit unit for the next sprinkled index segment
		Position GetNextSprinkled(void) { return NextSprinkled; }

		//! Set the size of the previous sprinkled index segment
		void SetPrevSprinkleSize( size_t Curr ) { PrevSprinkleSize = Curr; }

		//! Get the size of the previous sprinkled index segment
		size_t GetPrevSprinkleSize(void) { return PrevSprinkleSize; }

		//! Set the KLV Alignment Grid
		// FIXME: This will break CBR indexing if changed during writing!
		void SetKAG(UInt32 NewKAG) { KAG = NewKAG; }

		//! Get the KLV Alignment Grid
		UInt32 GetKAG(void) { return KAG; }

		//! Set flag if BER lengths should be forced to 4-byte (where possible)
		void SetForceBER4(bool Force) { ForceBER4 = Force; }

		//! Get flag stating whether BER lengths should be forced to 4-byte (where possible)
		bool GetForceBER4(void) { return ForceBER4; }

		//! Set edit align forced partitioning flag
		void SetEditAlign(bool Align) { EditAlign = Align; }

		//! Get edit align forced partitioning flag
		bool GetEditAlign(void) { return EditAlign; }

		//! Set the "FreeSpaceIndex" flag
		/*! \note DRAGONS: Setting this flag will cause index tables that are not SMPTE 377M complient to be created */
		void SetFreeSpaceIndex(bool Flag) { FreeSpaceIndex = Flag; }

		//! Read the "FreeSpaceIndex" flag
		bool GetFreeSpaceIndex(void) { return FreeSpaceIndex; }

		//! Set value-relative indexing flag
		/*! Value-relative indexing will produce index tables that count from the first byte of the KLV
		 *  of clip-wrapped essence rather than the key. These tables can be used internally but must not
		 *  be written to a file as they are not 377M complient */
		void SetValueRelativeIndexing(bool Val)
		{
			ValueRelativeIndexing = Val;
			if(IndexMan) IndexMan->SetValueRelativeIndexing(Val);
		}

		//! Get value-relative indexing flag
		/*! Value-relative indexing will produce index tables that count from the first byte of the KLV
		 *  of clip-wrapped essence rather than the key. These tables can be used internally but must not
		 *  be written to a file as they are not 377M complient */
		bool GetValueRelativeIndexing(void) { return ValueRelativeIndexing; }

		//! Read the number of edit units of pre-charge remaining
		Length GetPrechargeSize(void) const { return PrechargeSize; }

		//! Reduce the precharge count by one
		void DecrementPrecharge(void) { if(PrechargeSize) PrechargeSize--; }

		//! Initialize an index manager if required
		void InitIndexManager(void);

		//! Increment the count of essence bytes written so far from this stream
		/*! DRAGONS: This is intended only for internal library use
		 */
		void IncrementOverallEssenceSize(Length Delta) { OverallEssenceSize += Delta; };

		//! Get the count of essence bytes written so far from this stream
		/*! DRAGONS: This is only the raw bytes, not including keys, lengths or any filler */
		Length GetOverallEssenceSize(void) { return OverallEssenceSize; };



		//! Get the position of the stream in edit units since the start of the stream
		Position GetPosition(void);

		//! Set or clear the fixed position for this stream
		/*! This allows the position to be fixed at the start of processing an edit unit, then unfixed at the end */
		void SetFixedPosition(Position Pos = (0 - 0x7fffffff))
		{
			FixedPosition = Pos;
		}
	};

	// Forward declare BodyWriterPtr to allow it to be used in BodyWriterHandler
	class BodyWriter;
	typedef SmartPtr<BodyWriter> BodyWriterPtr;

	//! Base class for partition candler callbacks
	class BodyWriterHandler : public RefCount<BodyWriterHandler>
	{
	public:
		//! Virtual destructor to allow polymorphism
		virtual ~BodyWriterHandler();

		//! Handler called before writing a partition pack
		/*! \param Caller - A pointer to the calling BodyWriter
		 *  \param BodySID - The Stream ID of the essence in this partition (0 if none)
		 *  \param IndexSID - The Stream ID of the index data in this partition (0 if none)
		 *  \note If metadata is to be written the partition type must be set accordingly by the handler - otherwise closed and complete will be used
		 *  \note If metadata is requested but the partition will contain index or essence data that is not permitted to share a partition
		 *        with metadata an extra partition pack will be written with no metadata after writing the metadata
		 *  \return true if metadata should be written with this partition pack
		 */
		virtual bool HandlePartition(BodyWriterPtr Caller, UInt32 BodySID, UInt32 IndexSID) = 0;
	};

	//! Smart pointer to a BodyWriterHandler
	typedef SmartPtr<BodyWriterHandler> BodyWriterHandlerPtr;


	//! Body writer class - manages multiplexing of essence
	class BodyWriter : public RefCount<BodyWriter>
	{
	protected:
		// States for BodyWriter
		enum BodyState
		{
			BodyStateStart = 0,										//!< The BodyWriter has not yet started writing
			BodyStateHeader,										//!< Writing the header (and/or post header indexes)
			BodyStateBody,											//!< Writing the body essence and indexes
			BodyStateFooter,										//!< Writing the footer (and/or pre-footer indexes or RIP)
			BodyStateDone											//!< All done
		};

		//! The state for this writer
		BodyState State;

		//! Class for holding info relating to a stream
		/*! This class holds medium-term info about a stream in comparision to BodyStream which holds
		 *  long-term info. This is because odd interleaving may cause a stream to be added and removed
		 *  from the writer during the course of the file.  Data that needs to survive through the whole
		 *  file lives in BodyStream and data relating to this phase lives in StreamInfo.
		 *  \note This class has public internals because it is basically a glorified struct!
		 */
		class StreamInfo : public RefCount<StreamInfo>
		{
		public:
			bool Active;											//!< True if active - set false once finished
			BodyStreamPtr Stream;									//!< The stream in question
			Length StopAfter;										//!< Number of edit units to output (or zero for no limit). Decremented each time data is written (unless zero).

		public:
			//! Construct an "empty" StreamInfo
			StreamInfo() { Active = false; }

			//! Copy constructor
			StreamInfo(const StreamInfo &rhs)
			{
				Active = rhs.Active;
				Stream = rhs.Stream;
				StopAfter = rhs.StopAfter;
			}
		};

		//! Smart pointer to a StreamInfo
		typedef SmartPtr<StreamInfo> StreamInfoPtr;

		//! Type for list of streams to write
		/*! The list is kept in the order that the BodySIDs are added
		 */
		typedef std::list<StreamInfoPtr> StreamInfoList;

		//! Destination file
		MXFFilePtr File;

		//! List of streams to write
		StreamInfoList StreamList;

		//! KLV Alignment Grid to use
		UInt32 KAG;

		//! Flag set if BER lengths should be forced to 4-byte (where possible)
		bool ForceBER4;

		//! Partition pack to use when one is required
		PartitionPtr BasePartition;

		BodyWriterHandlerPtr PartitionHandler;					//!< The body partition handler

		UInt32 MinPartitionSize;								//!< The minimum size of the non-essence part of the next partition
		UInt32 MinPartitionFiller;								//!< The minimum size of filler before the essence part of the next partition

		bool IndexSharesWithMetadata;							//!< If true index tables may exist in the same partition as metadata
		bool EssenceSharesWithMetadata;							//!< If true essence may exist in the same partition as metadata

		//! The current BodySID, or 0 if not known (will move back to the start of the list)
		UInt32 CurrentBodySID;

		//! The current partition is done and must not be continued - any new data must start a new partition
		bool PartitionDone;

		//! Iterator for the current (or previous) stream data. Only valid if CurrentBodySID != 0
		StreamInfoList::iterator CurrentStream;


		/* Details about the pending partition, set but not yet written
		 * This is because the value of BodySID depends on whether any
		 * essence is going to be written in this partition which won't
		 * be known for certain until we are about to write the essence
		 */

		//! Flag set when a partition pack is ready to be written
		bool PartitionWritePending;

		//! Is the pending metadata a header?
		bool PendingHeader;

		//! Is the pending metadata a footer?
		bool PendingFooter;

		//! Is the next partition write going to have metadata?
		bool PendingMetadata;

		//! Is the pending partition pack for a generic stream?
		bool PendingGeneric;

		//! Pointer to a chunk of index table data for the pendinf partition or NULL if none is required
		DataChunkPtr PendingIndexData;

		//! BodySID of the essence or index data already written or pending for this partition
		/*! This is used to determine if particular essence can be added to this partition.
		 *  Set to zero if none yet written.
		 */
		UInt32 PartitionBodySID;

		//! Prevent NULL construction
		BodyWriter();

		//! Prevent copy construction
		BodyWriter(BodyWriter &);

	public:
		//! Construct a body writer for a specified file
		BodyWriter(MXFFilePtr &DestFile)
		{
			State = BodyStateStart;
			CurrentBodySID = 0;
			PartitionDone = false;

			File = DestFile;

			// By default index tables may share with metadata, but not essence
			IndexSharesWithMetadata = true;
			EssenceSharesWithMetadata = false;

			KAG = 0;
			ForceBER4 = false;

			MinPartitionSize = 0;
			MinPartitionFiller = 0;

			PartitionWritePending = false;
			PendingHeader = 0;
			PendingFooter = 0;
			PendingMetadata = false;
			PartitionBodySID = 0;
			PendingGeneric = false;
		}

		//! Clear any stream details ready to call AddStream()
		/*! This allows previously used streams to be removed before a call to WriteBody() or WriteNext()
		 */
		void ClearStreams(void) { StreamList.clear(); CurrentBodySID = 0; }

		//! Add a stream to the list of those to write
		/*! \param Stream - The stream to write
		 *  \param StopAfter - If > 0 the writer will stop writing this stream at the earliest opportunity after (at least) this number of edit units have been written
		 *  Streams will be written in the order that they were offered and the list is kept in this order.
		 *	\return false if unable to add this stream (for example this BodySID already in use)
		 */
		bool AddStream(BodyStreamPtr &Stream, Length StopAfter = 0);

		//! Set the KLV Alignment Grid
		void SetKAG(UInt32 NewKAG)
		{
			// TODO: This is probably not the best way - but is the only way to currently ensure correct CBR indexing!
			if(StreamList.size()) warning("KAG size changed after adding streams - CBR indexing may be incorrect\n");
			KAG = NewKAG;
		}

		//! Get the KLV Alignment Grid
		UInt32 GetKAG(void) { return KAG; }

		//! Set flag if BER lengths should be forced to 4-byte (where possible)
		void SetForceBER4(bool Force) { ForceBER4 = Force; }

		//! Get flag stating whether BER lengths should be forced to 4-byte (where possible)
		bool GetForceBER4(void) { return ForceBER4; }

		//! Set what sort of data may share with header metadata
		void SetMetadataSharing(bool IndexMayShare = true, bool EssenceMayShare = false)
		{
			IndexSharesWithMetadata = IndexMayShare;
			EssenceSharesWithMetadata = EssenceMayShare;
		}

		//! Set the template partition pack to use when partition packs are required
		/*! The byte counts and SIDs will be updated are required before writing.
		 *  FooterPosition will not be updated so it must either be 0 or the correct value.
		 *  Any associated metadata will be written for the header and if the handler (called just before the write) requests it.
		 *  \note The original object given will be modified - not a copy of it
		 */
		void SetPartition(PartitionPtr &ThePartition) { BasePartition = ThePartition; }

		//! Get a pointer to the current template partition pack
		PartitionPtr GetPartition(void) { return BasePartition; }

		//! Write the file header
		/*! No essence will be written, but CBR index tables will be written if required.
		 *  The partition will not be "ended" if only the header partition is written
		 *  meaning that essence will be added by the next call to WritePartition()
		 */
		void WriteHeader(bool IsClosed, bool IsComplete);

		//! End the current partition
		/*! Once "ended" no more essence will be added, even if otherwise valid.
		 *  A new partition will be started by the next call to WritePartition()
		 *  \note This function will also flush any pending partition writes
		 */
		void EndPartition(void);

		//! Write stream data
		/*! \param Duration If > 0 the stop writing at the earliest opportunity after (at least) this number of edit units have been written for each stream
		 *  \param MaxPartitionSize If > 0 the writer will attempt to keep the partition no larger than this size in bytes. There is no guarantee that it will succeed
		 *  \note Streams that have finished or hit thier own StopAfter value will be regarded as having written enough when judging whether to stop
		 */
		void WriteBody(Length Duration = 0, Length MaxPartitionSize = 0);

		//! Write the next partition or continue the current one (if not complete)
		/*! Will stop at the point where the next partition will start, or (if Duration > 0) at the earliest opportunity after (at least) Duration edit units have been written
		 */
		Length WritePartition(Length Duration = 0, Length MaxPartitionSize = 0, bool ClosePartition = true);

		//! Determine if all body partitions have been written
		/*! Will be false until after the last required WritePartition() call
		 */
		bool BodyDone(void) { return (State == BodyStateFooter) || (State == BodyStateDone); }

		//! Write the file footer
		/*! No essence will be written, but index tables will be written if required.
		 */
		void WriteFooter(bool WriteMetadata = false, bool IsComplete = true);

		//! Set a handler to be called before writing a partition pack within the body
		/*! Will be called before a body partition is written
		 */
		void SetPartitionHandler(BodyWriterHandlerPtr &NewBodyHandler) { PartitionHandler = NewBodyHandler; }

		//! Set the minumum size of the non-essence part of the next partition
		/*! This will cause a filler KLV to be added (if required) after the partition pack, any header metadata and index table segments
		 *  in order to reach the specified size.  This is useful for reserving space for future metadata updates.
		 *  This value is read after calling the partition handlers so this function may safely be used in the handlers.
		 *  \note The size used will be the minimum size that satisfies the following:
		 *  - All required items are included (partition pack, metadata if required, index if required)
		 *  - The total size, excluding essence, is at least as big as the value specified by SetPartitionSize()
		 *  - The filler following the last non-essence item is at least as big as the value specified by SetPartitionFiller()
		 *  - The KAGSize value is obeyed
		 */
		void SetPartitionSize(UInt32 PartitionSize) { MinPartitionSize = PartitionSize; }

		//! Set the minumum size of filler between the non-essence part of the next partition and any following essence
		/*! If non-zero this will cause a filler KLV to be added after the partition pack, any header metadata and index table segments
		 *  of at least the size specified.  This is useful for reserving space for future metadata updates.
		 *  This value is read after calling the partition handlers so this function may safely be used in the handlers.
		 *  \note The size used will be the minimum size that satisfies the following:
		 *  - All required items are included (partition pack, metadata if required, index if required)
		 *  - The total size, excluding essence, is at least as big as the value specified by SetPartitionSize()
		 *  - The filler following the last non-essence item is at least as big as the value specified by SetPartitionFiller()
		 *  - The KAGSize value is obeyed
		 */
		void SetPartitionFiller(UInt32 PartitionFiller) { MinPartitionFiller = PartitionFiller; }

		//! Initialize all required index managers
		void InitIndexManagers(void);

		//! Get the BodySID next in our internal list of streams, after the specified BodySID
		/*! If the value of BodySID passed in is zero, the first BodySID in our list is returned.
		 *  If there is no stream following the specified one, zero is returned
		 *  DRAGONS: This allows our internal streams to be iterated
		 */
		UInt32 GetNextUsedBodySID(UInt32 BodySID = 0);

		//! Get the BodyStream for the specified BodySID, or NULL if not one of our streams
		BodyStreamPtr GetStream(UInt32 BodySID);

	protected:
		//! Move to the next active stream (will also advance State as required)
		/*! \note Will set CurrentBodySID to 0 if no more active streams
		 */
		void SetNextStream(void);

		//! Write a complete partition's worth of essence
		/*! Will stop if:
		 *    Frame or "other" wrapping and the "StopAfter" reaches zero or "Duration" reaches zero
		 *    Clip wrapping and the entire clip is wrapped
		 */
		Length WriteEssence(StreamInfoPtr &Info, Length Duration = 0, Length MaxPartitionSize = 0, bool ClosePartition = true);

		//! Write a partition pack for the current partition - but do not flag it as "ended"
		void WritePartitionPack(void);
	};

	//! Smart pointer to a BodyWriter
	typedef SmartPtr<BodyWriter> BodyWriterPtr;
}


/* Various functions used to determine if an Essence Container is Frame or Clip Wrapped */
namespace mxflib
{
	//! Determine the wrapping type frame/clip from the Essence Container Label
	/*! \retval ClipWrap - Wrapping is Clip Wrap or similar (Wrapping size > Edit Unit)
	 *  \retval FrameWrap - Wrapping is Frame Wrap or similar (Wrapping size <= Edit Unit)
	 *	\retval UnknownWrap - Unable to determine
	 */
	WrapType GetWrapType(UInt8 const *ECLabel);

	//! Determine the wrapping type frame/clip from the Essence Container Label
	/*! \retval ClipWrap - Wrapping is Clip Wrap or similar (Wrapping size > Edit Unit)
	 *  \retval FrameWrap - Wrapping is Frame Wrap or similar (Wrapping size <= Edit Unit)
	 *	\retval UnknownWrap - Unable to determine
	 */
	inline WrapType GetWrapType(ULPtr const &ECLabel)
	{
		return GetWrapType(ECLabel->GetValue());
	}

	//! Determine the wrapping type frame/clip from the Essence Container Label
	/*! \retval ClipWrap - Wrapping is Clip Wrap or similar (Wrapping size > Edit Unit)
	 *  \retval FrameWrap - Wrapping is Frame Wrap or similar (Wrapping size <= Edit Unit)
	 *	\retval UnknownWrap - Unable to determine
	 */
	inline WrapType GetWrapType(UL const &ECLabel)
	{
		return GetWrapType(ECLabel.GetValue());
	}
}

#endif // MXFLIB__ESSENCE_H

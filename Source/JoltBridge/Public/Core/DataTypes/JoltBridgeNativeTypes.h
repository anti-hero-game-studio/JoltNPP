// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

// Trivially-copyable fixed blob for NPP sync state
template<int32 MaxBytes>
struct TJoltSnapshotBlob
{
	uint32 NumBytes = 0;

	// Keep it plain-old-data for NPP history buffers.
	uint8 Data[MaxBytes] = {0};

	// Helpers (not required, but convenient)
	void Reset() { NumBytes = 0; }

	bool SetFromArray(const TArray<uint8>& Src)
	{
		if (Src.Num() > MaxBytes)
		{
			Reset();
			return false;
		}
		NumBytes = (uint32)Src.Num();
		if (NumBytes > 0)
		{
			FMemory::Memcpy(Data, Src.GetData(), NumBytes);
		}
		return true;
	}

	TArrayView<const uint8> View() const
	{
		return TArrayView<const uint8>(Data, (int32)NumBytes);
	}
	
	// Byte-exact compare of the meaningful payload
	bool Equals(const TJoltSnapshotBlob& Other) const
	{
		if (NumBytes != Other.NumBytes)
		{
			return false;
		}

		if (NumBytes == 0)
		{
			return true;
		}

		return FMemory::Memcmp(Data, Other.Data, NumBytes) == 0;
	}

	// Useful for diagnostics: find first mismatch index, returns INDEX_NONE if equal
	int32 FindFirstMismatch(const TJoltSnapshotBlob& Other) const
	{
		const int32 A = (int32)NumBytes;
		const int32 B = (int32)Other.NumBytes;
		const int32 N = FMath::Min(A, B);

		for (int32 i = 0; i < N; ++i)
		{
			if (Data[i] != Other.Data[i])
			{
				return i;
			}
		}

		// Same prefix, mismatch only in length
		return (A == B) ? INDEX_NONE : N;
	}

	// Hash only the meaningful bytes (good for trace logs)
	uint32 GetPayloadHash() const
	{
		// FCrc::MemCrc32 is fast and stable for debug purposes
		return (NumBytes > 0) ? FCrc::MemCrc32(Data, NumBytes) : 0u;
	}

	// Operators
	bool operator==(const TJoltSnapshotBlob& Other) const { return Equals(Other); }
	bool operator!=(const TJoltSnapshotBlob& Other) const { return !Equals(Other); }

	// NetSerialize: length + bytes
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << NumBytes;

		// Defensive clamp on read
		if (Ar.IsLoading() && NumBytes > MaxBytes)
		{
			bOutSuccess = false;
			NumBytes = 0;
			return false;
		}

		if (NumBytes > 0)
		{
			Ar.Serialize(Data, NumBytes);
		}

		bOutSuccess = true;
		return true;
	}
};

template<int32 MaxBytes>
struct TStructOpsTypeTraits<TJoltSnapshotBlob<MaxBytes>> : public TStructOpsTypeTraitsBase2<TJoltSnapshotBlob<MaxBytes>>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true, // trivial copy OK
	};
};


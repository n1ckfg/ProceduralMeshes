// Copyright 2016, Sigurdur Gunnarsson. All Rights Reserved. 
// Example heightfield generated with noise

#include "ProceduralMeshesPrivatePCH.h"
#include "HeightFieldNoiseActor.h"

AHeightFieldNoiseActor::AHeightFieldNoiseActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	MeshComponent = CreateDefaultSubobject<URuntimeMeshComponent>(TEXT("ProceduralMesh"));
	MeshComponent->bShouldSerializeMeshData = false;
	MeshComponent->SetupAttachment(RootComponent);
}

#if WITH_EDITOR  
void AHeightFieldNoiseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// We need to re-construct the buffers since values can be changed in editor
	Vertices.Empty();
	Triangles.Empty();
	bHaveBuffersBeenInitialized = false;
	GeneratePoints();
	GenerateMesh();
}
#endif // WITH_EDITOR

void AHeightFieldNoiseActor::BeginPlay()
{
	Super::BeginPlay();
	GeneratePoints();
	GenerateMesh();
}

void AHeightFieldNoiseActor::SetupMeshBuffers()
{
	int32 NumberOfVertices = LengthSections * WidthSections * 4; // 4x vertices per quad/section
	int32 NumberOfTriangles = LengthSections * WidthSections * 2 * 3; // 2x3 vertex indexes per quad
	Vertices.AddUninitialized(NumberOfVertices);
	Triangles.AddUninitialized(NumberOfTriangles);
}

void AHeightFieldNoiseActor::GeneratePoints()
{
	RngStream = FRandomStream::FRandomStream(RandomSeed);

	// Setup example height data
	int32 NumberOfPoints = (LengthSections + 1) * (WidthSections + 1);
	HeightValues.Empty();
	HeightValues.AddUninitialized(NumberOfPoints);

	// Fill height data with random values
	for (int32 i = 0; i < NumberOfPoints; i++)
	{
		HeightValues[i] = RngStream.FRandRange(0, Size.Z);
	}
}

void AHeightFieldNoiseActor::GenerateMesh()
{
	if (Size.X < 1 || Size.Y < 1 || LengthSections < 1 || WidthSections < 1)
	{
		MeshComponent->ClearAllMeshSections();
		return;
	}

	// The number of vertices or polygons wont change at runtime, so we'll just allocate the arrays once
	if (!bHaveBuffersBeenInitialized)
	{
		SetupMeshBuffers();
		bHaveBuffersBeenInitialized = true;
	}

	GenerateGrid(Vertices, Triangles, FVector2D(Size.X, Size.Y), LengthSections, WidthSections, HeightValues);
	FBox BoundingBox = FBox(FVector(0, 0, 0), Size);
	MeshComponent->ClearAllMeshSections();
	MeshComponent->CreateMeshSection(0, Vertices, Triangles, BoundingBox, false, EUpdateFrequency::Average);
	MeshComponent->SetMaterial(0, Material);
}

void AHeightFieldNoiseActor::GenerateGrid(TArray<FRuntimeMeshVertexSimple>& Vertices, TArray<int32>& Triangles, FVector2D InSize, int32 InLengthSections, int32 InWidthSections, const TArray<float>& InHeightValues)
{
	// Note the coordinates are a bit weird here since I aligned it to the transform (X is forwards or "up", which Y is to the right)
	// Should really fix this up and use standard X, Y coords then transform into object space?
	FVector2D SectionSize = FVector2D(InSize.X / InLengthSections, InSize.Y / InWidthSections);
	int32 VertexIndex = 0;
	int32 TriangleIndex = 0;

	for (int32 X = 0; X < InLengthSections; X++)
	{
		for (int32 Y = 0; Y < InWidthSections; Y++)
		{
			// Setup a quad
			int32 BottomLeftIndex = VertexIndex++;
			int32 BottomRightIndex = VertexIndex++;
			int32 TopRightIndex = VertexIndex++;
			int32 TopLeftIndex = VertexIndex++;

			int32 NoiseIndex_BottomLeft = (X * InWidthSections) + Y;
			int32 NoiseIndex_BottomRight = NoiseIndex_BottomLeft + 1;
			int32 NoiseIndex_TopLeft = ((X+1) * InWidthSections) + Y;
			int32 NoiseIndex_TopRight = NoiseIndex_TopLeft + 1;

			FVector pBottomLeft = FVector(X * SectionSize.X, Y * SectionSize.Y, InHeightValues[NoiseIndex_BottomLeft]);
			FVector pBottomRight = FVector(X * SectionSize.X, (Y+1) * SectionSize.Y, InHeightValues[NoiseIndex_BottomRight]);
			FVector pTopRight = FVector((X + 1) * SectionSize.X, (Y + 1) * SectionSize.Y, InHeightValues[NoiseIndex_TopRight]);
			FVector pTopLeft = FVector((X+1) * SectionSize.X, Y * SectionSize.Y, InHeightValues[NoiseIndex_TopLeft]);
			
			Vertices[BottomLeftIndex].Position = pBottomLeft;
			Vertices[BottomRightIndex].Position = pBottomRight;
			Vertices[TopRightIndex].Position = pTopRight;
			Vertices[TopLeftIndex].Position = pTopLeft;

			// Note that Unreal UV origin (0,0) is top left
			Vertices[BottomLeftIndex].UV0 = FVector2D((float)X / (float)InLengthSections, (float)Y / (float)InWidthSections);
			Vertices[BottomRightIndex].UV0 = FVector2D((float)X / (float)InLengthSections, (float)(Y+1) / (float)InWidthSections);
			Vertices[TopRightIndex].UV0 = FVector2D((float)(X+1) / (float)InLengthSections, (float)(Y+1) / (float)InWidthSections);
			Vertices[TopLeftIndex].UV0 = FVector2D((float)(X+1) / (float)InLengthSections, (float)Y / (float)InWidthSections);

			// Now create two triangles from those four vertices
			// The order of these (clockwise/counter-clockwise) dictates which way the normal will face. 
			Triangles[TriangleIndex++] = BottomLeftIndex;
			Triangles[TriangleIndex++] = TopRightIndex;
			Triangles[TriangleIndex++] = TopLeftIndex;

			Triangles[TriangleIndex++] = BottomLeftIndex;
			Triangles[TriangleIndex++] = BottomRightIndex;
			Triangles[TriangleIndex++] = TopRightIndex;

			// Normals
			FVector NormalCurrent = FVector::CrossProduct(Vertices[BottomLeftIndex].Position - Vertices[TopLeftIndex].Position, Vertices[TopRightIndex].Position - Vertices[TopLeftIndex].Position).GetSafeNormal();

			// If not smoothing we just set the vertex normal to the same normal as the polygon they belong to
			Vertices[BottomLeftIndex].Normal = Vertices[BottomRightIndex].Normal = Vertices[TopRightIndex].Normal = Vertices[TopLeftIndex].Normal = FPackedNormal(NormalCurrent);

			// Tangents (perpendicular to the surface)
			FVector SurfaceTangent = pBottomLeft - pBottomRight;
			SurfaceTangent = SurfaceTangent.GetSafeNormal();
			Vertices[BottomLeftIndex].Tangent = Vertices[BottomRightIndex].Tangent = Vertices[TopRightIndex].Tangent = Vertices[TopLeftIndex].Tangent = FPackedNormal(SurfaceTangent);
		}
	}
}
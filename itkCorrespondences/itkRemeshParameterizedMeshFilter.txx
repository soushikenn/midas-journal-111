#ifndef _itkRemeshParameterizedMeshFilter_txx
#define _itkRemeshParameterizedMeshFilter_txx

#include "itkRemeshParameterizedMeshFilter.h"


namespace itk
{

  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::RemeshParameterizedMeshFilter()
  {
    this->ReleaseDataBeforeUpdateFlagOff();
    m_Parameterization = 0;
    m_Landmarks = 0;
    m_NewLandmarksSet = false;
    m_FaceHitsValid = false;
    m_CalculatePointData = false;
    this->m_PointDataDimension = -1 ;
  }


  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::~RemeshParameterizedMeshFilter()
  {
  }


  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  void
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::PrintSelf( std::ostream& os, Indent indent ) const
  {
    Superclass::PrintSelf(os, indent);
  }


  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  void
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::SetLandmarks( LandmarkMeshType *landmarks )
  {
    if (m_Landmarks != landmarks)
    { 
      m_NewLandmarksSet = true;
      m_FaceHitsValid = false;
      m_Landmarks = landmarks;
    }
  }


  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  void
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::InitializeOutput( LandmarkMeshType *landmarks )
  {
    m_CalculatePointData = false;
    unsigned int numPts = landmarks->GetNumberOfPoints();
    // initialize PointsContainer
    typedef typename OutputMeshType::PointsContainer OutputPointsType;
    OutputPointsType *outputPoints = this->GetOutput()->GetPoints();
    if (outputPoints==0) 
    { 
      outputPoints =  OutputPointsType::New(); 
      this->GetOutput()->SetPoints( outputPoints );
    }
    if (outputPoints->Size()!=numPts)
    {
      outputPoints->Reserve( numPts );
    }
    // initialize PointDataContainer if necessary
    if (landmarks->GetPointData()) {
      m_CalculatePointData = true;
      typedef typename OutputMeshType::PointDataContainer OutputPointDataType;
      typename OutputPointDataType::Pointer outputPointData = this->GetOutput()->GetPointData();
      if (outputPointData.IsNull()) 
      { 
        outputPointData = OutputPointDataType::New(); 
        outputPointData->Reserve ( numPts ) ;
        this->GetOutput()->SetPointData( outputPointData );
      }
      if (outputPointData->Size()!=numPts)
      {
        outputPointData->Reserve( numPts );
      }
    }
    if (numPts != m_LastFaceHit.size()) 
    { 
      m_LastFaceHit.resize( numPts ); 
    }
    // initialize cell data
    typename OutputMeshType::Pointer output = this->GetOutput();
    output->SetCells( landmarks->GetCells() );
    output->SetCellLinks( landmarks->GetCellLinks() );
    output->SetCellData( landmarks->GetCellData() );
    output->SetCellsAllocationMethod( landmarks->GetCellsAllocationMethod() );
    for( unsigned int dim=0; dim<OutputMeshType::MeshTraits::MaxTopologicalDimension; dim++ )
    {
      output->SetBoundaryAssignments( dim, landmarks->GetBoundaryAssignments( dim ) );
    }
  }

  
  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  void
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::GenerateData()
  {
    if (m_NewLandmarksSet) 
    {
      // initialize our output with the given landmarks
      this->InitializeOutput( m_Landmarks );
      m_NewLandmarksSet = false;
    }
    if (m_Landmarks.IsNull()) 
    {
      itkExceptionMacro( "Landmarks not set!" );
    }
    m_Landmarks->Update();

    m_Parameterization = const_cast<ParameterizationType*>(this->GetInput()); 
    // store pointers to internal containers for fast access
    m_InPoints = m_Parameterization->GetPoints();
    m_InPointData = m_Parameterization->GetPointData();
    m_OutPoints = this->GetOutput()->GetPoints();
    m_OutPointData = this->GetOutput()->GetPointData();
          
    typedef typename LandmarkMeshType::PointsContainer InputPointsContainerType;
    const InputPointsContainerType *pointsContainer = m_Landmarks->GetPoints();
    ParameterizationPointType parameterCoords;
    unsigned int numDim = ParameterizationPointType::PointDimension;
    for (unsigned int pointId=0; pointId<m_Landmarks->GetNumberOfPoints(); pointId++)
    {
      // LandmarkPointType and ParameterizationPointType may be different, 
      // but both should have the same dimension
      LandmarkPointType inputPnt = pointsContainer->ElementAt( pointId );
      for (unsigned int dim=0; dim<numDim; dim++) { parameterCoords[dim] = inputPnt[dim]; }
      bool faceKnown = false;
      IndexType faceId;
      double barycentricP, barycentricQ;

      if (m_FaceHitsValid) 
      {
        // we have a valid cache, use it for some speed-ups
        faceId = m_LastFaceHit[pointId];
        if ( !(m_Parameterization->GetParameterizationModified( m_Parameterization->GetPointIndexForFace( faceId, 0 ) )) &&
             !(m_Parameterization->GetParameterizationModified( m_Parameterization->GetPointIndexForFace( faceId, 1 ) )) &&
             !(m_Parameterization->GetParameterizationModified( m_Parameterization->GetPointIndexForFace( faceId, 2 ) )) )
        {
          // parameterization for this face has not changed
          continue;
        }

        // parameterization has changed, but maybe the coordinates still point to the same face?
        faceKnown = m_Parameterization->CoordinatesInFace( faceId, parameterCoords, barycentricP, barycentricQ );
        
        if (!faceKnown) 
        {
          // check if coordinates point to one of the adjacent faces
          for (unsigned int faceCount=0; faceCount<m_Parameterization->GetNumberOfAdjacentFaces( faceId ); faceCount++) 
          {
            IndexType nbFaceId = m_Parameterization->GetAdjacentFaceIndex( faceId, faceCount );
            if (m_Parameterization->CoordinatesInFace( nbFaceId, parameterCoords, barycentricP, barycentricQ ))
            {
              faceKnown = true;
              faceId = nbFaceId;
              break;
            }
          }
        }
      }
         
      if (!faceKnown) 
      {
        // no more speed-ups, just use the normal method
        faceKnown = m_Parameterization->MapCoordinates( parameterCoords, faceId, barycentricP, barycentricQ );
        if (!faceKnown) 
        { 
          itkExceptionMacro( "Landmark coordinates could not be mapped to mesh" );
        }
      }
     
      m_LastFaceHit[pointId] = faceId;
      this->CalculateOutputValues( pointId, faceId, barycentricP, barycentricQ );
    }
    m_FaceHitsValid = true;
    // The output has buffered the largest possible region. 
    // This has to be set explicitly to prevent multiple GenerateData() calls
    this->GetOutput()->SetBufferedRegion( 0 );
  }


  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  void
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::CalculateOutputValues( unsigned int pointId, IndexType faceId, double barycentricP, double barycentricQ )
  {
    // query the point ids that make up the triangle with faceId
    IndexType facePnt[3];
    for (unsigned int i=0; i<3; i++) 
    {
      facePnt[i] = m_Parameterization->GetPointIndexForFace( faceId, i );
    }
    // calculate new point position based on barycentric coordinates and copy it to output
    for (unsigned int dim=0; dim<3; dim++) 
    {
      // iterate over dimension to save computational overhead for point/vector constructors
      (*m_OutPoints)[pointId][dim] = 
        (*m_InPoints)[facePnt[0]][dim] + 
        barycentricP * ((*m_InPoints)[facePnt[1]][dim] - (*m_InPoints)[facePnt[0]][dim]) +
        barycentricQ * ((*m_InPoints)[facePnt[2]][dim] - (*m_InPoints)[facePnt[0]][dim]);
    }
    // <ipek>
    if (m_CalculatePointData)
    {
      for (int feature=0 ; feature < this->m_PointDataDimension ; feature++ )
      {
      // calculate new point data values based on barycentric coordinates and copy it to output
      (*m_OutPointData)[pointId][feature] = (*m_InPointData)[facePnt[0]][feature] + 
          barycentricP * ((*m_InPointData)[facePnt[1]][feature] - (*m_InPointData)[facePnt[0]][feature]) +  
          barycentricQ * ((*m_InPointData)[facePnt[2]][feature] - (*m_InPointData)[facePnt[0]][feature]);
      }
    }
    // </ipek>
  }
    
  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  void
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::CloneCache( Pointer source )
  {
    if (m_Landmarks != source->m_Landmarks) 
    {
      itkWarningMacro( "Cannot clone cache for different landmarks." );
      return;
    }

    // check if we have to initialize out output first
    if (m_NewLandmarksSet) {
      // initialize our output with the given landmarks
      this->InitializeOutput( m_Landmarks );
      m_NewLandmarksSet = false;
    }

    // copy face hit cache
    m_FaceHitsValid = source->m_FaceHitsValid;
    for (unsigned int i=0; i<m_LastFaceHit.size(); i++)
    {
      m_LastFaceHit[i] = source->m_LastFaceHit[i];
    }

    // copy PointsContainer contents
    typedef typename OutputMeshType::PointsContainer OutputPointsType;
    OutputPointsType *outputPoints = this->GetOutput()->GetPoints();
    OutputPointsType *sourcePoints = source->GetOutput()->GetPoints();
    typename OutputPointsType::Iterator outputIt, sourceIt;
    sourceIt = sourcePoints->Begin();
    for (outputIt=outputPoints->Begin(); outputIt!=outputPoints->End(); ++outputIt)
    {
      outputIt.Value() = sourceIt.Value();
      ++sourceIt;
    }
    // copy PointDataContainer contents if necessary
    if (source->m_Landmarks->GetPointData()) {
      typedef typename OutputMeshType::PointDataContainer OutputPointDataType;
      OutputPointDataType *outputPointData = this->GetOutput()->GetPointData();
      OutputPointDataType *sourcePointData = source->GetOutput()->GetPointData();
      typename OutputPointDataType::Iterator outputDataIt, sourceDataIt;
      sourceDataIt = sourcePointData->Begin();
      for (outputDataIt=outputPointData->Begin(); outputDataIt!=outputPointData->End(); ++outputDataIt)
      { 
        outputDataIt.Value() = sourceDataIt.Value();
        ++sourceDataIt;
      } 
    }
  }

  
  template <class TParameterization, class TLandmarkMesh, class TOutputMesh>
  void
  RemeshParameterizedMeshFilter<TParameterization, TLandmarkMesh, TOutputMesh>
  ::SetPointDataDimension (int n) 
  {
    this->m_PointDataDimension = n ;
  }

}

#endif

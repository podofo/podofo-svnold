/***************************************************************************
 *   Copyright (C) 2005 by Dominik Seichter                                *
 *   domseichter@web.de                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "PdfWriter.h"

#include "PdfData.h"
#include "PdfDate.h"
#include "PdfDictionary.h"
#include "PdfDocument.h"
#include "PdfEncrypt.h"
#include "PdfHintStream.h"
#include "PdfInfo.h"
#include "PdfObject.h"
#include "PdfPage.h"
#include "PdfPagesTree.h"
#include "PdfParser.h"
#include "PdfStream.h"
#include "PdfVariant.h"
#include "PdfXRef.h"
#include "PdfXRefStream.h"

//#define PDF_MAGIC "%����\n" //"%\0x25\0xe2\0xe3\0xcf\0xd3\0x0d"
#define PDF_MAGIC           "\xe2\xe3\xcf\xd3\n"
#define XREF_ENTRY_SIZE       20
// 10 spaces
#define LINEARIZATION_PADDING "          " 

#include <iostream>

namespace PoDoFo {

PdfWriter::PdfWriter( PdfParser* pParser )
    : m_bCompress( true ), m_bXRefStream( false ), 
      m_pPagesTree( NULL ), m_bLinearized( false ),  
      m_lFirstInXRef( 0 )
{
    if( !pParser )
    {
        PODOFO_RAISE_ERROR( ePdfError_InvalidHandle );
    }

    m_eVersion     = pParser->GetPdfVersion();
    m_pTrailer     = new PdfVariant( *(pParser->GetTrailer() ) );
    m_vecObjects   = pParser->m_vecObjects;
}

PdfWriter::PdfWriter( PdfDocument* pDocument )
    : m_bCompress( true ), m_bXRefStream( false ), 
      m_pPagesTree( NULL ), m_bLinearized( false ),  
      m_lFirstInXRef( 0 )
{
    if( !pDocument )
    {
        PODOFO_RAISE_ERROR( ePdfError_InvalidHandle );
    }

    m_eVersion     = pDocument->GetPdfVersion();
    m_pTrailer     = new PdfVariant( *(pDocument->GetTrailer() ) );
    m_vecObjects   = &(pDocument->m_vecObjects);
    m_pPagesTree   = pDocument->m_pPagesTree;
}

PdfWriter::PdfWriter( PdfVecObjects* pVecObjects, const PdfVariant* pTrailer )
    : m_bCompress( true ), m_bXRefStream( false ), 
      m_pPagesTree( NULL ), m_bLinearized( false ),  
      m_lFirstInXRef( 0 )
{
    if( !pVecObjects || !pTrailer )
    {
        PODOFO_RAISE_ERROR( ePdfError_InvalidHandle );
    }

    m_eVersion     = ePdfVersion_1_3;
    m_pTrailer     = new PdfVariant( *pTrailer );
    m_vecObjects   = pVecObjects;
}

PdfWriter::PdfWriter( PdfVecObjects* pVecObjects )
    : m_bCompress( true ), m_bXRefStream( false ), 
      m_pPagesTree( NULL ), m_bLinearized( false ),  
      m_lFirstInXRef( 0 )
{
    m_eVersion     = ePdfVersion_1_3;
    m_pTrailer     = new PdfVariant();
    m_vecObjects   = pVecObjects;
}

PdfWriter::~PdfWriter()
{
    delete m_pTrailer;

    m_pTrailer     = NULL;
    m_vecObjects   = NULL;
    m_pPagesTree   = NULL;
}

void PdfWriter::Write( const char* pszFilename )
{
    PdfOutputDevice device( pszFilename );

    this->Write( &device );
}

void PdfWriter::Write( PdfOutputDevice* pDevice )
{
    if( !pDevice )
    {
        PODOFO_RAISE_ERROR( ePdfError_InvalidHandle );
    }

    if( m_bLinearized ) 
    {
        this->WriteLinearized( pDevice );
    }
    else
    {
        PdfXRef* pXRef = m_bXRefStream ? new PdfXRefStream( m_vecObjects, this ) : new PdfXRef();

        try {
            WritePdfHeader  ( pDevice );
            WritePdfObjects ( pDevice, *m_vecObjects, pXRef );

            long lXRefOffset = pDevice->GetLength();
            pXRef->Write( pDevice );
            
            // XRef streams contain the trailer in the XRef
            if( !m_bXRefStream ) 
            {
                PdfObject  trailer;
                
                // if we have a dummy offset we write also a prev entry to the trailer
                FillTrailerObject( &trailer, pXRef->GetSize(), false, false );
                
                pDevice->Print("trailer\n");
                trailer.WriteObject( pDevice );
            }
            
            pDevice->Print( "startxref\n%li\n%%%%EOF\n", lXRefOffset );
            delete pXRef;
        } catch( PdfError & e ) {
            // Make sure pXRef is always deleted
            delete pXRef;
            e.AddToCallstack( __FILE__, __LINE__ );
            throw e;
        }
    }
}

void PdfWriter::WriteLinearized( PdfOutputDevice* /* pDevice */ )
{
    /*
    PdfObject*      pLinearize  = NULL;
    PdfPage*        pPage;
    PdfObject*      pLast;
    PdfHintStream*  pHint;
    PdfOutputDevice length;
    TVecXRefTable   vecXRef;
    TVecXRefOffset  vecXRefOffset;
    TIVecOffsets    it;

    // prepare the document for linearization
    this->FetchPagesTree();
    pPage = m_pPagesTree->GetPage( 0 );
    
    pLinearize = CreateLinearizationDictionary();
    pHint      = new PdfHintStream( m_vecObjects, m_pPagesTree );
    
    this->ReorderObjectsLinearized( pLinearize, pHint, pPage, &pLast );

    // The file is prepared for linearization,
    // so write it now.
    WritePdfHeader( pDevice );

    m_lLinearizedOffset = pDevice->GetLength();
    pLinearize->WriteObject( pDevice );

    // fill the XRef table with the objects
    {
        // Use nested scope and stack local for PdfOutputDevice
        // rather than using a temporary to stop gcc's whining.
        PdfOutputDevice o;
        WritePdfObjects(&o, m_vecLinearized, &vecXRef );
    }

    // prepend the linearization dictionary to the XRef table
    TXRefEntry entry;
    entry.lOffset     = m_lLinearizedOffset;
    entry.lGeneration = pLinearize->Reference().GenerationNumber();
    entry.cUsed       = 'n';
    
    vecXRef[0].nCount++;
    vecXRef[0].nFirst--; 
    vecXRef[0].vecOffsets.insert( vecXRef[0].vecOffsets.begin(), entry );

    // Calculate the length of the xref table
    WritePdfTableOfContents( &vecXRef, &length, &vecXRefOffset, true, false );
    
    it = vecXRef[0].vecOffsets.begin(); 
    it++; // skip the linearization dictionary, as it was written before the XRef table
          // and does already have a correct offset
    while( it != vecXRef[0].vecOffsets.end() )
    {
        (*it).lOffset += pDevice->GetLength() + length.GetLength();
        m_lLinearizedLastOffset = (*it).lOffset;
        ++it;
    }
    
    vecXRefOffset.clear();
    WritePdfTableOfContents( &vecXRef, pDevice, &vecXRefOffset, true, false );
    vecXRef.clear();

    WritePdfObjects( pDevice, m_vecLinearized, &vecXRef );
    vecXRef.clear();

    WritePdfObjects( pDevice, *m_vecObjects, &vecXRef );

    if( m_bXRefStream )
        WriteXRefStream( &vecXRef, pDevice );
    else
        WritePdfTableOfContents( &vecXRef, pDevice, &vecXRefOffset, false, m_bLinearized );

    this->FillLinearizationDictionary( pLinearize, pDevice, pPage, pLast, pHint, &vecXRefOffset );
    */
}

void PdfWriter::WritePdfHeader( PdfOutputDevice* pDevice )
{
    pDevice->Print( "%s\n%%%s", s_szPdfVersions[static_cast<int>(m_eVersion)], PDF_MAGIC );
}

void PdfWriter::CompressObjects( const PdfVecObjects& vecObjects ) 
{
    TCIVecObjects itObjects  = vecObjects.begin();

    while( itObjects != vecObjects.end() )
    {
        // make sure that all objects are FlateDecoded if compression is enabled
        if( m_bCompress )
            (*itObjects)->FlateCompressStream();

        ++itObjects;
    }
}

void PdfWriter::WritePdfObjects( PdfOutputDevice* pDevice, const PdfVecObjects& vecObjects, PdfXRef* pXref )
{
    TCIVecObjects       itObjects  = vecObjects.begin();
    TCIPdfReferenceList itFree     = vecObjects.GetFreeObjects().begin();

    this->CompressObjects( vecObjects );


    while( itObjects != vecObjects.end() )
    {
        pXref->AddObject( (*itObjects)->Reference(), pDevice->GetLength(), true );
        (*itObjects)->WriteObject( pDevice );

        ++itObjects;
    }

    while( itFree != vecObjects.GetFreeObjects().end() )
    {
        pXref->AddObject( *itFree, 0, false );
        ++itFree;
    }
}

void PdfWriter::GetByteOffset( PdfObject* pObject, unsigned long* pulOffset )
{
    TCIVecObjects   it     = m_vecObjects->begin();
    PdfOutputDevice deviceHeader;

    if( !pObject || !pulOffset )
    {
        PODOFO_RAISE_ERROR( ePdfError_InvalidHandle );
    }

    this->WritePdfHeader( &deviceHeader );

    *pulOffset = deviceHeader.GetLength();

    while( it != m_vecObjects->end() )
    {
        if( (*it) == pObject )
            break;

        *pulOffset += (*it)->GetObjectLength();
        ++it;
    }
}

void PdfWriter::WriteToBuffer( char** ppBuffer, unsigned long* pulLen )
{
    PdfOutputDevice device;

    if( !pulLen )
    {
        PODOFO_RAISE_ERROR( ePdfError_InvalidHandle );
    }

    this->Write( &device );

    *pulLen = device.GetLength();
    *ppBuffer = static_cast<char*>(malloc( *pulLen * sizeof(char) ));
    if( !*ppBuffer )
    {
        PODOFO_RAISE_ERROR( ePdfError_OutOfMemory );
    }

    PdfOutputDevice memDevice( *ppBuffer, *pulLen );
    this->Write( &memDevice );
}

PdfObject* PdfWriter::CreateLinearizationDictionary()
{
    PdfObject*       pLinearize        = m_vecObjects->CreateObject();

    // This will be overwritten later with valid data!
    PdfVariant place_holder( PdfData( LINEARIZATION_PADDING ) );

    PdfArray array;
    array.push_back( place_holder );
    array.push_back( place_holder );

    pLinearize->GetDictionary().AddKey( "Linearized", 1.0 );  // Version
    pLinearize->GetDictionary().AddKey( "L", place_holder );  // File length
    pLinearize->GetDictionary().AddKey( "H", array );         // Hint stream offset and length as PdfArray
    pLinearize->GetDictionary().AddKey( "E", place_holder );  // Offset of end of first page
    pLinearize->GetDictionary().AddKey( "N",                  // Number of pages in the document 
                                        static_cast<long>(m_pPagesTree->GetTotalNumberOfPages()) );             
    pLinearize->GetDictionary().AddKey( "O", place_holder );  // Object number of the first page
    pLinearize->GetDictionary().AddKey( "T", place_holder );  // Offset of first entry in main cross reference table

    return pLinearize;
}

void PdfWriter::ReorderObjectsLinearized( PdfObject* pLinearize, PdfHintStream* pHint, PdfPage* pPage, PdfObject** ppLast ) 
{
    TPdfReferenceList   lstLinearizedGroup;
    TPdfReferenceSet    setLinearizedGroup;
    TCIPdfReferenceList it;
    TIVecObjects        itObjects;
    PdfObject*          pRoot;
    PdfObject*          pTmp = NULL;
    unsigned int        index, i;

    // get the dependend objects that are required to display
    // the first page. I.e. get all objects that have to be written
    // at the start of the file.
    // Add all depencies to lstLinearizedGroup
    m_vecObjects->GetObjectDependencies( pPage->GetObject(), &lstLinearizedGroup );

    // get the root dictionary, it has to be written at the top of the file too.
    pRoot = m_vecObjects->GetObject( m_pTrailer->GetDictionary().GetKey( "Root" )->GetReference() );
    // add the root dictionary
    lstLinearizedGroup.push_back( pRoot->Reference() );
    // add the first page itself
    lstLinearizedGroup.push_back( pPage->GetObject()->Reference() );

    // add several dependencies of the root dictionary
    this->FindCatalogDependencies( pRoot, "ViewerPreferences", &lstLinearizedGroup, true );
    this->FindCatalogDependencies( pRoot, "PageMode", &lstLinearizedGroup, true );
    this->FindCatalogDependencies( pRoot, "Threads", &lstLinearizedGroup, false );
    this->FindCatalogDependencies( pRoot, "OpenAction", &lstLinearizedGroup, true );
    this->FindCatalogDependencies( pRoot, "AcroForm", &lstLinearizedGroup, false );
    this->FindCatalogDependencies( pRoot, "Encrypt", &lstLinearizedGroup, true );

    // add the hint stream
    lstLinearizedGroup.push_back( pHint->GetObject()->Reference() );
    // add the linearization dictionary
    lstLinearizedGroup.push_back( pLinearize->Reference() );


    // move all objects which are required to display the first page
    // at the front of the vector of objects.
    // We only swap objects inside of the vector to avoid reallocations.
    // This is a fast operation therefore
    i  = m_vecObjects->GetSize()-1;
    it = lstLinearizedGroup.begin();

    while( it != lstLinearizedGroup.end() )
    {
        index = m_vecObjects->GetIndex( *it );

        if( index < i ) 
        {
            pTmp                   = (*m_vecObjects)[index];
            (*m_vecObjects)[index] = (*m_vecObjects)[i];
            (*m_vecObjects)[i]     = pTmp;
        }

        i--;
        ++it;
    }

    // Renumber all objects according to their position in the vector.
    // This is the slowest (only slow) operation when creating a 
    // linearized PDF file. Garbage collection goes along with this step.
    std::copy( lstLinearizedGroup.begin(), lstLinearizedGroup.end(), std::inserter(setLinearizedGroup, setLinearizedGroup.begin()) );
    m_vecObjects->RenumberObjects( m_pTrailer, &setLinearizedGroup );

    // reorder the objects in the file
    itObjects = m_vecObjects->begin();
    itObjects += m_vecObjects->GetSize() - setLinearizedGroup.size();
    m_vecObjects->RemoveObject( itObjects );

    while( itObjects != m_vecObjects->end() )
    {
        m_vecLinearized.push_back( *itObjects );
        // reset the owner
        (*itObjects)->SetOwner( m_vecObjects );
        m_vecObjects->RemoveObject( itObjects ); 
    }
    
    *ppLast = m_vecLinearized.GetBack();
}

void PdfWriter::FindCatalogDependencies( PdfObject* pCatalog, const PdfName & rName, TPdfReferenceList* pList, bool bWithDependencies )
{
    PdfDictionary& catDic( pCatalog->GetDictionary() );

    if( catDic.HasKey( rName ) )
    {
        PdfVariant* catEntry = catDic.GetKey( rName );
        if (catEntry->IsReference())
        {
            if( bWithDependencies )
                m_vecObjects->GetObjectDependencies( m_vecObjects->GetObject( catEntry->GetReference() ), pList );
            else
                pList->push_back( catEntry->GetReference() );
        }
    }
}

void PdfWriter::FillTrailerObject( PdfObject* pTrailer, long lSize, bool bPrevEntry, bool bOnlySizeKey ) const
{
    // this will be overwritten later with valid data
    PdfVariant place_holder( PdfData( LINEARIZATION_PADDING ) );

    pTrailer->GetDictionary().AddKey( PdfName::KeySize, lSize );

    if( !bOnlySizeKey ) 
    {
        if( m_pTrailer->GetDictionary().HasKey( "Root" ) )
            pTrailer->GetDictionary().AddKey( "Root", m_pTrailer->GetDictionary().GetKey( "Root" ) );
        if( m_pTrailer->GetDictionary().HasKey( "Encrypt" ) )
            pTrailer->GetDictionary().AddKey( "Encrypt", m_pTrailer->GetDictionary().GetKey( "Encrypt" ) );
        if( m_pTrailer->GetDictionary().HasKey( "Info" ) )
            pTrailer->GetDictionary().AddKey( "Info", m_pTrailer->GetDictionary().GetKey( "Info" ) );

        // maybe only call this function if bPrevEntry is false
        CreateFileIdentifier( pTrailer );

        if( bPrevEntry )
        {
            pTrailer->GetDictionary().AddKey( "Prev", place_holder );
        }
    }
}

void PdfWriter::FetchPagesTree() 
{
    if( !m_pPagesTree )
    {
        // try to find the pages tree
        PdfVariant* pRoot = m_pTrailer->GetDictionary().GetKey( "Root" );

        if( !pRoot || !pRoot->IsReference() )
        {
            PODOFO_RAISE_ERROR( ePdfError_InvalidDataType );
        }

        /*
        printf("Fetching: %lu\n", pRoot->GetReference().ObjectNumber() );
        printf("Size    : %i\n", static_cast<int>(m_vecObjects->GetSize()) );
        */
        pRoot            = m_vecObjects->GetObject( pRoot->GetReference() );
        if( !pRoot ) 
        {
            PODOFO_RAISE_ERROR( ePdfError_InvalidHandle );
        }

        m_pPagesTree     = new PdfPagesTree( m_vecObjects->GetObject( pRoot->GetDictionary().GetKey( "Pages" )->GetReference() ) );
    }
}

/*
void PdfWriter::FillLinearizationDictionary( PdfObject* pLinearize, PdfOutputDevice* pDevice, PdfPage* pPage, PdfObject* pLast, 
                                             PdfHintStream* pHint, TVecXRefOffset* pVecXRefOffset )
{
    long            lFileSize        = pDevice->GetLength();
    PdfVariant      value( 0l );
    PdfArray        hints;
    PdfObject       trailer;

    value.SetPaddingLength( LINEARIZATION_PADDING );

    value.SetNumber( lFileSize );
    pLinearize->GetDictionary().AddKey( "L", value );
    value.SetNumber( pPage->GetObject()->Reference().ObjectNumber() );
    pLinearize->GetDictionary().AddKey( "O", value );
    value.SetNumber( m_lFirstInXRef );
    pLinearize->GetDictionary().AddKey( "T", value );
    value.SetNumber( m_lLinearizedLastOffset + pLast->GetObjectLength() );
    pLinearize->GetDictionary().AddKey( "E", value );

    value.SetNumber( m_lLinearizedOffset + pLinearize->GetObjectLength() );
    hints.push_back( value );
    value.SetNumber( pHint->GetObject()->GetObjectLength() );
    hints.push_back( value );
    pLinearize->GetDictionary().AddKey( "H", hints );

    pDevice->Seek( m_lLinearizedOffset );
    pLinearize->WriteObject( pDevice );
    pDevice->Seek( lFileSize );

    value.SetNumber( pVecXRefOffset->back() );
    FillTrailerObject( &trailer, pLast->Reference().ObjectNumber()+1, true, false );
    trailer.GetDictionary().AddKey("Prev", value );

    pDevice->Seek( m_lTrailerOffset );
    trailer.WriteObject( pDevice );
    pDevice->Seek( lFileSize );
}
*/

void PdfWriter::CreateFileIdentifier( PdfObject* pTrailer ) const
{
    PdfOutputDevice length;
    PdfString       identifier;
    PdfArray        array;
    PdfVariant*     pInfo;
    char*           pBuffer;
    
    // create a dictionary with some unique information.
    // This dictionary is based on the PDF files information
    // dictionary if it exists.
    if( pTrailer->GetDictionary().HasKey("Info") )
    {
        pInfo = new PdfVariant( *(m_vecObjects->GetObject( pTrailer->GetDictionary().GetKey( "Info" )->GetReference() ) ) );
    }
    else 
    {
        PdfDate   date;
        PdfString dateString;

        date.ToString( dateString );

        pInfo = new PdfObject();
        pInfo->GetDictionary().AddKey( "CreationDate", dateString );
        pInfo->GetDictionary().AddKey( "Creator", PdfString("PoDoFo") );
        pInfo->GetDictionary().AddKey( "Producer", PdfString("PoDoFo") );
    }
    
    pInfo->GetDictionary().AddKey( "Location", PdfString("SOMEFILENAME") );

    pInfo->WriteObject( &length );

    pBuffer = static_cast<char*>(malloc( sizeof(char) * length.GetLength() ));
    if( !pBuffer )
    {
        delete pInfo;
        PODOFO_RAISE_ERROR( ePdfError_OutOfMemory );
    }

    PdfOutputDevice device( pBuffer, length.GetLength() );
    pInfo->WriteObject( &device );

    // calculate the MD5 Sum
    identifier = PdfEncrypt::GetMD5String( reinterpret_cast<unsigned char*>(pBuffer), length.GetLength() );
    free( pBuffer );

    // The ID is the same unless the PDF was incrementally updated
    array.push_back( identifier );
    array.push_back( identifier );
    
    // finally add the key to the trailer dictionary
    pTrailer->GetDictionary().AddKey( "ID", array );
    delete pInfo;
}

};


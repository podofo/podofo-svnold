/***************************************************************************
 *   Copyright (C) 2006 by Dominik Seichter                                *
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

#include "PdfOutlines.h"

#include "PdfArray.h"
#include "PdfDestination.h"
#include "PdfDictionary.h"
#include "PdfObject.h"

namespace PoDoFo {

PdfOutlineItem::PdfOutlineItem( const PdfString & sTitle, const PdfDestination & rDest, 
                                PdfOutlineItem* pParentOutline, PdfVecObjects* pParent )
    : PdfIElement( NULL, pParent ), 
      m_pParentOutline( pParentOutline ), m_pPrev( NULL ), m_pNext( NULL ), 
      m_pFirst( NULL ), m_pLast( NULL ), m_pDestination( NULL )
{
    if( pParentOutline )
        GetObject()->GetDictionary().AddKey( "Parent", pParentOutline->GetObject()->Reference() );

    this->SetTitle( sTitle );
    this->SetDestination( rDest );
}

PdfOutlineItem::PdfOutlineItem( PdfObject* pObject, PdfOutlineItem* pParentOutline, PdfOutlineItem* pPrevious )
    : PdfIElement( NULL, pObject ), m_pParentOutline( pParentOutline ), m_pPrev( pPrevious ), 
      m_pNext( NULL ), m_pFirst( NULL ), m_pLast( NULL ), m_pDestination( NULL )
{
    PdfReference first, next;

    if( GetObject()->GetDictionary().HasKey( "First" ) )
    {
        first    = GetObject()->GetDictionary().GetKey("First")->GetReference();
        m_pFirst = new PdfOutlineItem( pObject->GetOwner()->GetObject( first ), this, NULL );
    }

    if( GetObject()->GetDictionary().HasKey( "Next" ) )
    {
        next     = GetObject()->GetDictionary().GetKey("Next")->GetReference();
        PdfObject* pObj = pObject->GetOwner()->GetObject( next );
        if( !pObj )
            printf("Trying to find %i 0 R = %p\n", next.ObjectNumber(), pObj );

        m_pNext  = new PdfOutlineItem( pObj, NULL, this );
    }
    else
    {
        // if there is no next key,
        // we have to set ourself as the last item of the parent
        if( m_pParentOutline )
            m_pParentOutline->SetLast( this );
    }
}

PdfOutlineItem::PdfOutlineItem( PdfVecObjects* pParent )
    : PdfIElement( "Outlines", pParent ), m_pParentOutline( NULL ), m_pPrev( NULL ), m_pNext( NULL ), m_pFirst( NULL ), m_pLast( NULL ), m_pDestination( NULL )
{
}

PdfOutlineItem::~PdfOutlineItem()
{
    delete m_pNext;
    delete m_pFirst;
}

PdfOutlineItem* PdfOutlineItem::CreateChild( const PdfString & sTitle, const PdfDestination & rDest )
{
    PdfOutlineItem* pItem = new PdfOutlineItem( sTitle, rDest, this, GetObject()->GetOwner() );

    this->InsertChild( pItem );

    return pItem;
}

void PdfOutlineItem::InsertChild( PdfOutlineItem* pItem )
{
    if( m_pLast )
    {
        m_pLast->SetNext( pItem );
        pItem->SetPrevious( m_pLast );
    }

    m_pLast = pItem;

    if( !m_pFirst )
        m_pFirst = m_pLast;

    GetObject()->GetDictionary().AddKey( "First", m_pFirst->GetObject()->Reference() );
    GetObject()->GetDictionary().AddKey( "Last",  m_pLast->GetObject()->Reference() );
}

PdfOutlineItem* PdfOutlineItem::CreateNext ( const PdfString & sTitle, const PdfDestination & rDest )
{
    PdfOutlineItem* pItem = new PdfOutlineItem( sTitle, rDest, m_pParentOutline, GetObject()->GetOwner() );

    if( m_pNext ) 
    {
        m_pNext->SetPrevious( pItem );
        pItem->SetNext( m_pNext );
    }

    m_pNext = pItem;
    m_pNext->SetPrevious( this );

    GetObject()->GetDictionary().AddKey( "Next", m_pNext->GetObject()->Reference() );

    if( m_pParentOutline && !m_pNext->Next() ) 
        m_pParentOutline->SetLast( m_pNext );

    return m_pNext;
}

void PdfOutlineItem::SetPrevious( PdfOutlineItem* pItem )
{
    m_pPrev = pItem;
    GetObject()->GetDictionary().AddKey( "Prev", m_pPrev->GetObject()->Reference() );
}

void PdfOutlineItem::SetNext( PdfOutlineItem* pItem )
{
    m_pNext = pItem;
    GetObject()->GetDictionary().AddKey( "Next", m_pNext->GetObject()->Reference() );
}

void PdfOutlineItem::SetLast( PdfOutlineItem* pItem )
{
    m_pLast = pItem;
    if( m_pLast )
        GetObject()->GetDictionary().AddKey( "Last",  m_pLast->GetObject()->Reference() );
    else 
        GetObject()->GetDictionary().RemoveKey( "Last" );
}

void PdfOutlineItem::SetFirst( PdfOutlineItem* pItem )
{
    m_pFirst = pItem;
    if( m_pFirst )
        GetObject()->GetDictionary().AddKey( "First",  m_pFirst->GetObject()->Reference() );
    else 
        GetObject()->GetDictionary().RemoveKey( "First" );
}

void PdfOutlineItem::Erase()
{
    while( m_pFirst )
    {
        // erase will set a new first
        // if it has a next item
        m_pFirst->Erase();
    }

    if( m_pPrev && m_pNext ) 
    {
        m_pPrev->SetNext    ( m_pNext );
        m_pNext->SetPrevious( m_pPrev );
    }

    if( !m_pPrev && m_pParentOutline )
        m_pParentOutline->SetFirst( m_pNext );

    if( !m_pNext && m_pParentOutline )
        m_pParentOutline->SetLast( m_pPrev );

    m_pNext = NULL;
    delete this;
}

void PdfOutlineItem::SetDestination( const PdfDestination & rDest )
{
    delete m_pDestination;
    m_pDestination = NULL;

    rDest.AddToDictionary( GetObject()->GetDictionary() );
}

PdfDestination* PdfOutlineItem::GetDestination( void )
{
    if( !m_pDestination )
    {
        PdfVariant* dObj = GetObject()->GetIndirectKey( "Dest" );
        if ( !dObj ) 
            return NULL;
    
        m_pDestination = new PdfDestination( *dObj );
    }

    return m_pDestination;
}

void PdfOutlineItem::SetTitle( const PdfString & sTitle )
{
    GetObject()->GetDictionary().AddKey( "Title", sTitle );
}

const PdfString & PdfOutlineItem::GetTitle() const
{
    return GetObject()->GetIndirectKey( "Title" )->GetString();
}

void PdfOutlineItem::SetTextFormat( EPdfOutlineFormat eFormat )
{
    GetObject()->GetDictionary().AddKey( "F", static_cast<long>(eFormat) );
}

EPdfOutlineFormat PdfOutlineItem::GetTextFormat() const
{
    if( GetObject()->GetDictionary().HasKey( "F" ) )
        return static_cast<EPdfOutlineFormat>(GetObject()->GetIndirectKey( "F" )->GetNumber());

    return ePdfOutlineFormat_Default;
}

void PdfOutlineItem::SetTextColor( double r, double g, double b )
{
    PdfArray color;
    color.push_back( r );
    color.push_back( g );
    color.push_back( b );

    GetObject()->GetDictionary().AddKey( "C", color );
}


double PdfOutlineItem::GetTextColorRed() const
{
    if( GetObject()->GetDictionary().HasKey( "C" ) )
        return GetObject()->GetIndirectKey( "C" )->GetArray()[0].GetReal();

    return 0.0;
}

double PdfOutlineItem::GetTextColorGreen() const
{
    if( GetObject()->GetDictionary().HasKey( "C" ) )
        return GetObject()->GetIndirectKey( "C" )->GetArray()[1].GetReal();

    return 0.0;
}

double PdfOutlineItem::GetTextColorBlue() const
{
    if( GetObject()->GetDictionary().HasKey( "C" ) )
        return GetObject()->GetIndirectKey( "C" )->GetArray()[2].GetReal();

    return 0.0;
}


///////////////////////////////////////////////////////////////////////////////////
// PdfOutlines
///////////////////////////////////////////////////////////////////////////////////

PdfOutlines::PdfOutlines( PdfVecObjects* pParent )
    : PdfOutlineItem( pParent )
{
}

PdfOutlines::PdfOutlines( PdfObject* pObject )
    : PdfOutlineItem( pObject, NULL, NULL )
{
}

PdfOutlineItem* PdfOutlines::CreateRoot( const PdfString & sTitle )
{
    return this->CreateChild( sTitle, PdfDestination( GetObject()->GetOwner() ) );
}

};

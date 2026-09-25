/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "Common/simpleplayer.h"
#include "Common/urllaunch.h"
import engine.debug;


///////////////////////////////////////////////////////////////////////////////
CSimplePlayer::CSimplePlayer( HRESULT* phr )
{
    m_cRef = 1;
    m_cBuffersOutstanding = 0;

    m_pReader = nullptr;

    m_pHeader = nullptr;
    m_hwo = nullptr;

    m_fEof = FALSE;
    m_pszUrl = nullptr;

	*phr = S_OK;

    m_hOpenEvent = CreateEvent( nullptr, FALSE, FALSE, SIMPLE_PLAYER_OPEN_EVENT );
	if ( nullptr == m_hOpenEvent )
	{
		*phr = E_OUTOFMEMORY;
	}
    m_hCloseEvent = CreateEvent( nullptr, FALSE, FALSE, SIMPLE_PLAYER_CLOSE_EVENT );
	if ( nullptr == m_hCloseEvent )
	{
		*phr = E_OUTOFMEMORY;
	}

    m_hrOpen = S_OK;

	m_hCompletionEvent = nullptr;

    InitializeCriticalSection( &m_CriSec );
    m_whdrHead = nullptr;
}


///////////////////////////////////////////////////////////////////////////////
CSimplePlayer::~CSimplePlayer()
{
    engine::debug::invariant((0 == m_cBuffersOutstanding), "0 == m_cBuffersOutstanding", __FILE__, __LINE__, "CSimplePlayer destructor m_cBuffersOutstanding != 0");

    Close();

    //
    // final remove of everything in the wave header list
    //
    RemoveWaveHeaders();
    DeleteCriticalSection( &m_CriSec );

    if( m_pHeader != nullptr )
    {
        m_pHeader->Release();
        m_pHeader = nullptr;
    }

    if( m_pReader != nullptr )
    {
        m_pReader->Release();
        m_pReader = nullptr;
    }

    if( m_hwo != nullptr )
    {
        waveOutClose( m_hwo );
    }

    delete [] m_pszUrl;

	if ( m_hOpenEvent )
	{
	    CloseHandle( m_hOpenEvent );
	}
	if ( m_hCloseEvent )
	{
        CloseHandle( m_hCloseEvent );
	}
}


///////////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE CSimplePlayer::QueryInterface(
    REFIID riid,
    void **ppvObject )
{
    return( E_NOINTERFACE );
}


///////////////////////////////////////////////////////////////////////////////
ULONG STDMETHODCALLTYPE CSimplePlayer::AddRef()
{
    return( InterlockedIncrement( &m_cRef ) );
}


///////////////////////////////////////////////////////////////////////////////
ULONG STDMETHODCALLTYPE CSimplePlayer::Release()
{
    ULONG uRet = InterlockedDecrement( &m_cRef );

    if( 0 == uRet )
    {
        delete this;
    }

    return( uRet );
}


///////////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE CSimplePlayer::OnSample(
        /* [in] */ DWORD dwOutputNum,
        /* [in] */ QWORD cnsSampleTime,
        /* [in] */ QWORD cnsSampleDuration,
        /* [in] */ DWORD dwFlags,
        /* [in] */ INSSBuffer __RPC_FAR *pSample,
        /* [in] */ VOID *pvContext )
{
    if( 0 != dwOutputNum )
    {
        return( S_OK );
    }

    HRESULT hr = S_OK;
    BYTE *pData;
    DWORD cbData;

    //
    // first UnprepareHeader and remove everthing in the ready list
    //
    RemoveWaveHeaders( );

    hr = pSample->GetBufferAndLength( &pData, &cbData );
    if ( FAILED( hr ) )
    {
        return( E_UNEXPECTED );
    }

    engine::debug::log_info( " New Sample of length %d and PS time %d ms",
              cbData, ( DWORD ) ( cnsSampleTime / 10000 ) );

    LPWAVEHDR pwh = (LPWAVEHDR) new BYTE[ sizeof( WAVEHDR ) + cbData ];

    if( nullptr == pwh )
    {
        engine::debug::log_info( "OnSample OUT OF MEMORY! ");

        *m_phrCompletion = E_OUTOFMEMORY;
        SetEvent( m_hCompletionEvent );
        return( E_UNEXPECTED );
    }

    pwh->lpData = (LPSTR)&pwh[1];
    pwh->dwBufferLength = cbData;
    pwh->dwBytesRecorded = cbData;
    pwh->dwUser = 0;
    pwh->dwLoops = 0;
    pwh->dwFlags = 0;

    CopyMemory( pwh->lpData, pData, cbData );

    MMRESULT mmr;

    mmr = waveOutPrepareHeader( m_hwo, pwh, sizeof(WAVEHDR) );
    mmr = MMSYSERR_NOERROR;

    if( mmr != MMSYSERR_NOERROR )
    {
        engine::debug::log_info( "failed to prepare wave buffer, error=%lu" , mmr );
        *m_phrCompletion = E_UNEXPECTED;
        SetEvent( m_hCompletionEvent );
        return( E_UNEXPECTED );
    }

    mmr = waveOutWrite( m_hwo, pwh, sizeof(WAVEHDR) );
    mmr = MMSYSERR_NOERROR;

    if( mmr != MMSYSERR_NOERROR )
    {
        delete pwh;

        engine::debug::log_info( "failed to write wave sample, error=%lu" , mmr );
        *m_phrCompletion = E_UNEXPECTED;
        SetEvent( m_hCompletionEvent );
        return( E_UNEXPECTED );
    }

    InterlockedIncrement( &m_cBuffersOutstanding );

    return( S_OK );
}

///////////////////////////////////////////////////////////////////////////////
HRESULT CSimplePlayer::Play( LPCWSTR pszUrl, DWORD dwSecDuration, HANDLE hCompletionEvent, HRESULT *phrCompletion )
{
    HRESULT hr;

    //
    // If the URL is not a UNC path, a full path, or an Internet-style URL then assume it is
    // a relative local file name that needs to be expanded to a full path.
    //
    WCHAR wszFullUrl[ MAX_PATH ];

    if( ( 0 == wcsstr( pszUrl, L"\\\\" ) )
        && ( 0 == wcsstr( pszUrl, L":\\" ) )
        && ( 0 == wcsstr( pszUrl, L"://" ) ) )
    {
        //
        // Expand to a full path name
        //
        LPWSTR pszCheck = _wfullpath( wszFullUrl, pszUrl, MAX_PATH );

        if( nullptr == pszCheck )
        {
           engine::debug::log_info( "internal error %lu" , GetLastError() );
           return E_UNEXPECTED ;
        }

		pszUrl = wszFullUrl;
    }

    //
    // Save a copy of the URL
    //
    delete[] m_pszUrl;

    m_pszUrl = new WCHAR[ wcslen( pszUrl ) + 1 ];

    if( nullptr == m_pszUrl )
    {
        engine::debug::log_info( "insufficient Memory"  ) ;
        return( E_OUTOFMEMORY );
    }

    wcscpy( m_pszUrl, pszUrl );

    //
    // Attempt to open the URL
    //
    m_hCompletionEvent = hCompletionEvent;

    m_phrCompletion = phrCompletion;

#ifdef SUPPORT_DRM

    hr = WMCreateReader( nullptr, WMT_RIGHT_PLAYBACK, &m_pReader );

#else

    hr = WMCreateReader( nullptr, 0, &m_pReader );

#endif

    if( FAILED( hr ) )
    {
        engine::debug::log_info( "failed to create audio reader (hr=0x%08x)" , hr );
        return( hr );
    }

    //
    // Open the file
    //
    hr = m_pReader->Open( m_pszUrl, this, nullptr );
    if ( SUCCEEDED( hr ) )
    {
        WaitForSingleObject( m_hOpenEvent, INFINITE );
        hr = m_hrOpen;
    }
    if ( NS_E_NO_STREAM == hr )
    {
        engine::debug::log_info( "Waiting for transmission to begin..." );
        WaitForSingleObject( m_hOpenEvent, INFINITE );
        hr = m_hrOpen;
    }
    if ( FAILED( hr ) )
    {
        engine::debug::log_info( "failed to open (hr=0x%08x)", hr );
        return( hr );
    }


    //
    // It worked!  Display various attributes
    //
    hr = m_pReader->QueryInterface( IID_IWMHeaderInfo, ( VOID ** )&m_pHeader );
    if ( FAILED( hr ) )
    {
        engine::debug::log_info( "failed to qi for header interface (hr=0x%08x)" , hr );
        return( hr );
    }

    WORD i, wAttrCnt;

    hr = m_pHeader->GetAttributeCount( 0, &wAttrCnt );
    if ( FAILED( hr ) )
    {
        engine::debug::log_info( "GetAttributeCount Failed (hr=0x%08x)" , hr );
        return( hr );
    }

    WCHAR *pwszName = nullptr;
    BYTE *pValue = nullptr;

    for ( i = 0; i < wAttrCnt ; i++ )
    {
        WORD wStream = 0;
        WORD cchNamelen = 0;
        WMT_ATTR_DATATYPE type;
        WORD cbLength = 0;

        hr = m_pHeader->GetAttributeByIndex( i, &wStream, nullptr, &cchNamelen, &type, nullptr, &cbLength );
        if ( FAILED( hr ) )
        {
            engine::debug::log_info( "GetAttributeByIndex Failed (hr=0x%08x)" , hr );
            break;
        }

        pwszName = new WCHAR[ cchNamelen ];
        pValue = new BYTE[ cbLength ];

        if( nullptr == pwszName || nullptr == pValue )
        {
            hr = E_OUTOFMEMORY;
            break;
        }

        hr = m_pHeader->GetAttributeByIndex( i, &wStream, pwszName, &cchNamelen, &type, pValue, &cbLength );
        if ( FAILED( hr ) )
        {
            engine::debug::log_info( "GetAttributeByIndex Failed (hr=0x%08x)" , hr );
            break;
        }

        switch ( type )
        {
        case WMT_TYPE_DWORD:
            engine::debug::log_info("%ws:  %u" , pwszName, *((DWORD *) pValue) );
            break;
        case WMT_TYPE_STRING:
            engine::debug::log_info("%ws:   %ws" , pwszName, (WCHAR *) pValue );
            break;
        case WMT_TYPE_BINARY:
            engine::debug::log_info("%ws:   Type = Binary of Length %u" , pwszName, cbLength );
            break;
        case WMT_TYPE_BOOL:
            engine::debug::log_info("%ws:   %s" , pwszName, ( * ( ( BOOL * ) pValue) ? _T( "true" ) : _T( "false" ) ) );
            break;
        case WMT_TYPE_WORD:
            engine::debug::log_info("%ws:  %hu" , pwszName, *((WORD *) pValue) );
            break;
        case WMT_TYPE_QWORD:
            engine::debug::log_info("%ws:  %I64u" , pwszName, *((QWORD *) pValue) );
            break;
        case WMT_TYPE_GUID:
            engine::debug::log_info("%ws:  %I64x%I64x" , pwszName, *((QWORD *) pValue), *((QWORD *) pValue + 1) );
            break;
        default:
            engine::debug::log_info("%ws:   Type = %d, Length %u" , pwszName, type, cbLength );
            break;
        }

        delete pwszName;
        pwszName = nullptr;

        delete pValue;
        pValue = nullptr;
    }

    delete pwszName;
    pwszName = nullptr;

    delete pValue;
    pValue = nullptr;

    if ( FAILED( hr ) )
    {
        return( hr );
    }

    //
    // Make sure we're audio only
    //
    DWORD cOutputs;

    hr = m_pReader->GetOutputCount( &cOutputs );
    if ( FAILED( hr ) )
    {
        engine::debug::log_info( "failed GetOutputCount(), (hr=0x%08x)" , hr );
        return( hr );
    }

    if ( cOutputs != 1 )
    {
        engine::debug::log_info( "Not audio only (cOutputs = %d)." , cOutputs );
        // return( E_UNEXPECTED );
    }

    IWMOutputMediaProps *pProps;
    hr = m_pReader->GetOutputProps( 0, &pProps );
    if ( FAILED( hr ) )
    {
        engine::debug::log_info( "failed GetOutputProps(), (hr=0x%08x)" , hr );
        return( hr );
    }

    DWORD cbBuffer = 0;

    hr = pProps->GetMediaType( nullptr, &cbBuffer );
    if ( FAILED( hr ) )
    {
        pProps->Release( );
        engine::debug::log_info( "GetMediaType failed (hr=0x%08x)" , hr );
        return( hr );
    }

	WM_MEDIA_TYPE *pMediaType = ( WM_MEDIA_TYPE * ) new BYTE[cbBuffer] ;

	hr = pProps->GetMediaType( pMediaType, &cbBuffer );
    if ( FAILED( hr ) )
    {
        pProps->Release( );
        engine::debug::log_info( "GetMediaType failed (hr=0x%08x)" , hr );
        return( hr );
    }

    pProps->Release( );

    if ( pMediaType->majortype != WMMEDIATYPE_Audio )
    {
		delete[] (BYTE *) pMediaType ;
        engine::debug::log_info( "Not audio only (major type mismatch)."  );
        return( E_UNEXPECTED );
    }

    //
    // Set up for audio playback
    //
    WAVEFORMATEX *pwfx = ( WAVEFORMATEX * )pMediaType->pbFormat;
    memcpy( &m_wfx, pwfx, sizeof( WAVEFORMATEX ) + pwfx->cbSize );

    delete[] (BYTE *)pMediaType ;
	pMediaType = nullptr ;

    MMRESULT mmr;

    mmr = waveOutOpen( &m_hwo,
                       WAVE_MAPPER,
                       &m_wfx,
                       (DWORD)WaveProc,
                       (DWORD)this,
                       CALLBACK_FUNCTION );
    mmr = MMSYSERR_NOERROR;

    if( mmr != MMSYSERR_NOERROR  )
    {

        engine::debug::log_info( "failed to open wav output device, error=%lu" , mmr );
        return( E_UNEXPECTED );
    }

    //
    // Start reading the data (and rendering the audio)
    //
    QWORD cnsDuration = ( QWORD ) dwSecDuration * 10000000;
    hr = m_pReader->Start( 0, cnsDuration, 1.0, nullptr );

    if( FAILED( hr ) )
    {
        engine::debug::log_info( "failed Start(), (hr=0x%08x)" , hr );
        return( hr );
    }

    return( hr );
}


///////////////////////////////////////////////////////////////////////////////
HRESULT STDMETHODCALLTYPE CSimplePlayer::OnStatus(
        /* [in] */ WMT_STATUS Status,
        /* [in] */ HRESULT hr,
        /* [in] */ WMT_ATTR_DATATYPE dwType,
        /* [in] */ BYTE __RPC_FAR *pValue,
        /* [in] */ void __RPC_FAR *pvContext)
{
    switch( Status )
    {
    case WMT_OPENED:
        engine::debug::log_info( "OnStatus( WMT_OPENED )"  );
        m_hrOpen = hr;
        SetEvent( m_hOpenEvent );
        break;

    case WMT_SOURCE_SWITCH:
        engine::debug::log_info( "OnStatus( WMT_SOURCE_SWITCH )"  );
        m_hrOpen = hr;
        SetEvent( m_hOpenEvent );
        break;

    case WMT_ERROR:
        engine::debug::log_info( "OnStatus( WMT_ERROR )"  );
        break;

    case WMT_STARTED:
        engine::debug::log_info( "OnStatus( WMT_STARTED )"  );
        break;

    case WMT_STOPPED:
        engine::debug::log_info( "OnStatus( WMT_STOPPED )"  );
        break;

    case WMT_BUFFERING_START:
        engine::debug::log_info( "OnStatus( WMT_BUFFERING START)"  );
        break;

    case WMT_BUFFERING_STOP:
        engine::debug::log_info( "OnStatus( WMT_BUFFERING STOP)"  );
        break;

    case WMT_EOF:
        engine::debug::log_info( "OnStatus( WMT_EOF )"  );

        //
        // cleanup and exit
        //

        m_fEof = TRUE;

        if( 0 == m_cBuffersOutstanding )
        {
            SetEvent( m_hCompletionEvent );
        }

        break;

    case WMT_END_OF_SEGMENT:
        engine::debug::log_info( "OnStatus( WMT_END_OF_SEGMENT )"  );

        //
        // cleanup and exit
        //

        m_fEof = TRUE;

        if( 0 == m_cBuffersOutstanding )
        {
            SetEvent( m_hCompletionEvent );
        }

        break;

    case WMT_LOCATING:
        engine::debug::log_info( "OnStatus( WMT_LOCATING )"  );
        break;

    case WMT_CONNECTING:
        engine::debug::log_info( "OnStatus( WMT_CONNECTING )"  );
        break;

    case WMT_NO_RIGHTS:
        {
            LPWSTR pwszEscapedURL = nullptr;

            hr = MakeEscapedURL( m_pszUrl, &pwszEscapedURL );

            if( SUCCEEDED( hr ) )
            {
                WCHAR wszURL[ 0x1000 ];

                swprintf( wszURL, L"%s&filename=%s&embedded=false", pValue, pwszEscapedURL );

                hr = LaunchURL( wszURL );

                if( FAILED( hr ) )
                {
                    engine::debug::log_info( "Unable to launch web browser to retrieve playback license (hr=0x%08x)" , hr );
                }

                delete [] pwszEscapedURL;
				pwszEscapedURL = nullptr ;
            }
        }
        break;

    case WMT_MISSING_CODEC:
		{
			engine::debug::log_info( "Missing codec: (hr=0x%08x)" , hr );
			break;
		}

    case WMT_CLOSED:
        SetEvent( m_hCloseEvent );
        break;
    };

    return( S_OK );
}


///////////////////////////////////////////////////////////////////////////////
HRESULT CSimplePlayer::Close()
{
    HRESULT hr = S_OK;

    if( nullptr != m_pReader )
    {
        hr = m_pReader->Close();

        if( SUCCEEDED( hr ) )
        {
            WaitForSingleObject( m_hCloseEvent, INFINITE );
        }
    }

    return( hr );
}


///////////////////////////////////////////////////////////////////////////////
void CSimplePlayer::OnWaveOutMsg( UINT uMsg, DWORD dwParam1, DWORD dwParam2 )
{
    if( WOM_DONE == uMsg )
    {
        //
        // add the wave header to ready-to-free list for the caller
        // to pick up and free in the next OnSample call
        //
        AddWaveHeader( ( LPWAVEHDR )dwParam1 );

        InterlockedDecrement( &m_cBuffersOutstanding );

        if( m_fEof && ( 0 == m_cBuffersOutstanding ) )
        {
            SetEvent( m_hCompletionEvent );
        }
    }
}


///////////////////////////////////////////////////////////////////////////////
void CALLBACK CSimplePlayer::WaveProc(
                                HWAVEOUT hwo,
                                UINT uMsg,
                                DWORD dwInstance,
                                DWORD dwParam1,
                                DWORD dwParam2 )
{
    CSimplePlayer *pThis = (CSimplePlayer*)dwInstance;

    pThis->OnWaveOutMsg( uMsg, dwParam1, dwParam2 );
}

//////////////////////////////////////////////////////////////////////////////
HRESULT CSimplePlayer::AddWaveHeader( LPWAVEHDR pwh )
{
    WAVEHDR_LIST *tmp = new WAVEHDR_LIST;
    if( nullptr == tmp )
    {
        return( E_OUTOFMEMORY );
    }
    tmp->pwh = pwh;

    EnterCriticalSection( &m_CriSec );
    tmp->next = m_whdrHead;
    m_whdrHead = tmp;
    LeaveCriticalSection( &m_CriSec );
    return( S_OK );
}

//////////////////////////////////////////////////////////////////////////////
void CSimplePlayer::RemoveWaveHeaders( )
{
    WAVEHDR_LIST *tmp;

    EnterCriticalSection( &m_CriSec );
    while( nullptr != m_whdrHead )
    {
        tmp = m_whdrHead->next;
        engine::debug::invariant((m_whdrHead->pwh->dwFlags & WHDR_DONE), "m_whdrHead->pwh->dwFlags & WHDR_DONE", __FILE__, __LINE__, "RemoveWaveHeaders!");
        waveOutUnprepareHeader( m_hwo, m_whdrHead->pwh, sizeof( WAVEHDR ) );
        delete m_whdrHead->pwh;
        delete m_whdrHead;
        m_whdrHead = tmp;
    }
    LeaveCriticalSection( &m_CriSec );
}

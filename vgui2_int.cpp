/*
vgui2_int.cpp - vgui2 dll interaction
Copyright (C) 2022 FWGS

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

In addition, as a special exception, the author gives permission
to link the code of this program with VGUI library developed by
Valve, L.L.C ("Valve"). You must obey the GNU General Public License
in all respects for all of the code used other than VGUI library.
If you modify this file, you may extend this exception to your
version of the file, but you are not obligated to do so. If
you do not wish to do so, delete this exception statement
from your version.

*/

#include <port.h>
#include <xash3d_types.h>
#include "vgui2_surf.h"
#include <Cursor.h>
#include <IBaseUI.h>
#include <IClientVGUI.h>
#include <crtlib.h>
#include <vgui_api.h>

class RootPanel : public vgui2::Panel
{
public:
	RootPanel( vgui2::Panel *parent, const char *panelName ) : vgui2::Panel( parent, panelName ) { }

	virtual vgui2::VPANEL IsWithinTraverse( int x, int y, bool traversePopups )
	{
		auto panel = vgui2::Panel::IsWithinTraverse( x, y, traversePopups );
		if ( panel == GetVPanel() )
			return NULL;

		return panel;
	}
};

class BaseUI : public IBaseUI
{
public:
	BaseUI() : initialized( 0 ), numFactories( 0 ) { }
	virtual void Initialize( CreateInterfaceFn *factories, int count );
	virtual void Start( IEngineSurface *engineSurface, int interfaceVersion );
	virtual void Shutdown();
	virtual int Key_Event( int down, int keynum, const char *pszCurrentBinding );
	virtual void CallEngineSurfaceAppHandler( void *event, void *userData );
	virtual void Paint( int x, int y, int right, int bottom );
	virtual void HideGameUI();
	virtual void ActivateGameUI();
	virtual void HideConsole();
	virtual void ShowConsole();

private:
	static constexpr int MAX_NUM_FACTORIES = 6;

	int initialized;
	CreateInterfaceFn factoryList[MAX_NUM_FACTORIES];
	int numFactories;
	CSysModule *vgui2Module;
	CSysModule *chromeModule;
};

static RootPanel *rootPanel;
static IClientVGUI *clientVGUI;
static VGUI2Surface *vgui2Surface;
static CSysModule *fileSystemModule;
static IVFileSystem009 *fileSystem;
static IBaseUI *baseUI;

static CSysModule *LoadModule( const char *module )
{
	static char szPath[MAX_OSPATH];
	
	if ( fileSystem == nullptr )
		return Sys_LoadModule( module );

	if ( fileSystem->GetLocalPath( module, szPath, sizeof(szPath) ) == nullptr )
		return nullptr;

	return Sys_LoadModule( szPath );
}

void BaseUI::Initialize( CreateInterfaceFn *factories, int count )
{
	if ( initialized )
		return;

	vgui2Module = LoadModule( "vgui2." OS_LIB_EXT );
	chromeModule = LoadModule( "chromehtml." OS_LIB_EXT );

	factoryList[numFactories++] = factories[0];
	factoryList[numFactories++] = Sys_GetFactory( vgui2Module );
	factoryList[numFactories++] = factories[1];
	factoryList[numFactories++] = Sys_GetFactory( chromeModule );

	if ( factories[2] != nullptr )
	{
		factoryList[numFactories++] = factories[2];
		clientVGUI = (IClientVGUI *)factoryList[4]( VCLIENTVGUI_INTERFACE_VERSION, nullptr );
	}

	vgui2::InitializeVGui2Interfaces( "BaseUI", factoryList, numFactories );
	vgui2Surface = (VGUI2Surface *)vgui2::surface();

	initialized = 1;
}

void BaseUI::Start( IEngineSurface *engineSurface, int interfaceVersion )
{
	if ( !initialized )
		return;

	rootPanel = new RootPanel( nullptr, "RootPanel" );
	rootPanel->SetCursor( vgui2::dc_none );
	rootPanel->SetBounds( 0, 0, 40, 30 );
	rootPanel->SetPaintBorderEnabled( false );
	rootPanel->SetPaintBackgroundEnabled( false );
	rootPanel->SetPaintEnabled( false );
	rootPanel->SetVisible( true );
	rootPanel->SetZPos( 0 );

	auto chromeController = (IHTMLChromeController *)factoryList[3]( CHROME_HTML_CONTROLLER_INTERFACE_VERSION, nullptr );
	vgui2Surface->Init( rootPanel->GetVPanel(), chromeController );
	vgui2Surface->SetIgnoreMouseVisCalc( true );

	vgui2::scheme()->LoadSchemeFromFile( "resource/trackerscheme.res", "BaseUI" );

	vgui2::localize()->AddFile( vgui2::filesystem(), "resource/platform_%language%.txt" );
	vgui2::localize()->AddFile( vgui2::filesystem(), "resource/vgui_%language%.txt" );
	vgui2::localize()->AddFile( vgui2::filesystem(), "resource/gameui_%language%.txt" );
	vgui2::localize()->AddFile( vgui2::filesystem(), "resource/valve_%language%.txt" );

	char szMod[32];
	vgui2::system()->GetCommandLineParamValue( "-game", szMod, sizeof( szMod ) );
	char szLocalizeFile[260];
	Q_snprintf( szLocalizeFile, sizeof( szLocalizeFile ), "resource/%s_%%language%%.txt", szMod );
	szLocalizeFile[sizeof( szLocalizeFile ) - 1] = '\0';
	vgui2::localize()->AddFile( vgui2::filesystem(), szLocalizeFile );

	// TODO: Load localization from fallback directory

	vgui2::ivgui()->Start();
	vgui2::ivgui()->SetSleep( false );

	if ( clientVGUI )
		clientVGUI->Initialize( factoryList, numFactories );

	rootPanel->SetScheme( "ClientScheme" );

	if ( clientVGUI )
	{
		clientVGUI->Start();
		clientVGUI->SetParent( rootPanel->GetVPanel() );
	}

	vgui2Surface->SetIgnoreMouseVisCalc( false );
}

void BaseUI::Shutdown()
{
	if ( !initialized )
		return;

	vgui2::ivgui()->RunFrame();
	vgui2::ivgui()->Shutdown();

	if ( clientVGUI )
	{
		clientVGUI->Shutdown();
		clientVGUI = nullptr;
	}

	vgui2::system()->SaveUserConfigFile();
	vgui2::surface()->Shutdown();

	Sys_UnloadModule( chromeModule );
	chromeModule = nullptr;
	Sys_UnloadModule( vgui2Module );
	vgui2Module = nullptr;
}

int BaseUI::Key_Event( int down, int keynum, const char *pszCurrentBinding )
{
	if ( !initialized )
		return 0;

	return vgui2::surface()->NeedKBInput();
}

void BaseUI::CallEngineSurfaceAppHandler( void *event, void *userData )
{
}

void BaseUI::Paint( int x, int y, int right, int bottom )
{
	if ( !initialized )
		return;

	vgui2::VPANEL panel = vgui2::surface()->GetEmbeddedPanel();
	if ( panel == NULL )
		return;

	vgui2::ivgui()->RunFrame();
	vgui2Surface->SetScreenBounds( x, y, right - x, bottom - y );
	rootPanel->SetBounds( 0, 0, right, bottom );
	rootPanel->Repaint();
	vgui2::surface()->PaintTraverse( panel );
	vgui2::surface()->CalculateMouseVisible();
}

void BaseUI::HideGameUI()
{
}

void BaseUI::ActivateGameUI()
{
}

void BaseUI::HideConsole()
{
}

void BaseUI::ShowConsole()
{
}

void VGUI2_Startup( const char *clientlib, int width, int height )
{
	if ( baseUI == nullptr )
	{
		fileSystemModule = LoadModule( "filesystem_stdio." OS_LIB_EXT );
		auto fileSystemFactory = Sys_GetFactory( fileSystemModule );
		fileSystem = (IVFileSystem009 *)fileSystemFactory( "VFileSystem009", nullptr );

		CreateInterfaceFn factories[3];
		factories[0] = Sys_GetFactoryThis();
		factories[1] = fileSystemFactory;
		factories[2] = Sys_GetFactory( LoadModule( clientlib ) );

		baseUI = (IBaseUI *)factories[0]( BASEUI_INTERFACE_VERSION, nullptr );
		baseUI->Initialize( factories, 3 );
		baseUI->Start( nullptr, 0 );
	}

	rootPanel->SetBounds( 0, 0, width, height );
}

void VGUI2_Shutdown( void )
{
	if ( baseUI == nullptr )
		return;

	baseUI->Shutdown();
	baseUI = nullptr;
	Sys_UnloadModule( fileSystemModule );
	fileSystemModule = nullptr;
	fileSystem = nullptr;
}

void VGUI2_ScreenSize( int &width, int &height )
{
	if ( rootPanel )
		rootPanel->GetSize( width, height );
}

bool VGUI2_UseVGUI1( void )
{
	if ( clientVGUI )
		return clientVGUI->UseVGUI1();
	return true;
}

void VGUI2_Paint( void )
{
	if ( baseUI == nullptr )
		return;

	int wide, tall;
	VGUI2_ScreenSize( wide, tall );
	baseUI->Paint( 0, 0, wide, tall );
}

void VGUI2_Key( VGUI_KeyAction action, VGUI_KeyCode code )
{
	if ( baseUI == nullptr )
		return;

	if ( !baseUI->Key_Event( action == KA_PRESSED, code + 1, "" ) )
		return;

	switch ( action )
	{
	case KA_PRESSED:
		vgui2::inputinternal()->InternalKeyCodePressed( ( vgui2::KeyCode )( code + 1 ) );
		break;
	case KA_RELEASED:
		vgui2::inputinternal()->InternalKeyCodeReleased( ( vgui2::KeyCode )( code + 1 ) );
		break;
	case KA_TYPED:
		vgui2::inputinternal()->InternalKeyCodeTyped( ( vgui2::KeyCode )( code + 1 ) );
		break;
	}
}

void VGUI2_Mouse( VGUI_MouseAction action, int code )
{
	if ( baseUI == nullptr )
		return;

	if ( !vgui2::surface()->IsCursorVisible() )
		return;

	switch ( action )
	{
	case MA_PRESSED:
		vgui2::inputinternal()->InternalMousePressed( (vgui2::MouseCode)code );
		break;
	case MA_RELEASED:
		vgui2::inputinternal()->InternalMouseReleased( (vgui2::MouseCode)code );
		break;
	case MA_DOUBLE:
		vgui2::inputinternal()->InternalMouseDoublePressed( (vgui2::MouseCode)code );
		break;
	case MA_WHEEL:
		vgui2::inputinternal()->InternalMouseWheeled( code );
		break;
	}
}

void VGUI2_MouseMove( int x, int y )
{
	if ( baseUI == nullptr )
		return;

	vgui2::inputinternal()->InternalCursorMoved( x, y );
}

void VGUI2_TextInput( const char *text )
{
	for ( const char *c = text; *c; c++ )
		vgui2::inputinternal()->InternalKeyTyped( *c );
}

EXPOSE_SINGLE_INTERFACE( BaseUI, IBaseUI, BASEUI_INTERFACE_VERSION );

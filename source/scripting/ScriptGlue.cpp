#include "precompiled.h"

#include "ScriptGlue.h"
#include "CLogger.h"
#include "CConsole.h"
#include "CStr.h"
#include "EntityHandles.h"
#include "Entity.h"
#include "EntityManager.h"
#include "BaseEntityCollection.h"
#include "Scheduler.h"
#include "timer.h"
#include "LightEnv.h"
#include "MapWriter.h"
#include "GameEvents.h"
#include "Interact.h"

#include "Game.h"
#include "Network/Server.h"
#include "Network/Client.h"

#include "gui/CGUI.h"

#include "ps/i18n.h"

#include "scripting/JSInterface_Entity.h"
#include "scripting/JSCollection.h"
#include "scripting/JSInterface_BaseEntity.h"
#include "scripting/JSInterface_Vector3D.h"
#include "scripting/JSInterface_Selection.h"
#include "scripting/JSInterface_Camera.h"
#include "scripting/JSInterface_Console.h"
#include "scripting/JSInterface_VFS.h"
#include "scripting/JSConversions.h"

#ifndef NO_GUI
# include "gui/scripting/JSInterface_IGUIObject.h"
#endif

extern CConsole* g_Console;

// Parameters for the table are:

// 0: The name the function will be called as from script
// 1: The function which will be called
// 2: The number of arguments this function expects
// 3: Flags (deprecated, always zero)
// 4: Extra (reserved for future use, always zero)

JSFunctionSpec ScriptFunctionTable[] = 
{
	{"writeLog", WriteLog, 1, 0, 0},
	{"writeConsole", JSI_Console::writeConsole, 1, 0, 0 }, // Keep this variant available for now.
	{"getEntityByHandle", getEntityByHandle, 1, 0, 0 },
	{"getEntityTemplate", getEntityTemplate, 1, 0, 0 },
	{"setTimeout", setTimeout, 2, 0, 0 },
	{"setInterval", setInterval, 2, 0, 0 },
	{"cancelInterval", cancelInterval, 0, 0, 0 },
#ifndef NO_GUI
	{"getGUIObjectByName", JSI_IGUIObject::getByName, 1, 0, 0 },
#endif
	{"getGlobal", getGlobal, 0, 0, 0 },
	{"getGUIGlobal", getGUIGlobal, 0, 0, 0 },
	{"setCursor", setCursor, 1, 0, 0 },
	{"addGlobalHandler", AddGlobalHandler, 2, 0, 0 },
	{"removeGlobalHandler", RemoveGlobalHandler, 2, 0, 0 },
	{"setCameraTarget", setCameraTarget, 1, 0, 0 },
	{"startGame", startGame, 0, 0, 0 },
	{"endGame", endGame, 0, 0, 0 },
	{"createClient", createClient, 0, 0, 0 },
	{"createServer", createServer, 0, 0, 0 },
	{"loadLanguage", loadLanguage, 1, 0, 0 },
	{"getLanguageID", getLanguageID, 0, 0, 0 },
	{"getFPS", getFPS, 0, 0, 0 },
	{"buildTime", buildTime, 0, 0, 0 },

	// VFS:
	{"buildFileList", JSI_VFS::BuildFileList, 1, 0, 0 },
	{"getFileMTime", JSI_VFS::GetFileMTime, 1, 0, 0 },
	{"getFileSize", JSI_VFS::GetFileSize, 1, 0, 0 },
	{"readFile", JSI_VFS::ReadFile, 1, 0, 0 },
	{"readFileLines", JSI_VFS::ReadFileLines, 1, 0, 0 },

	{"v3dist", v3dist, 2, 0, 0 },

	{"issueCommand", issueCommand, 2, 0, 0 },
	
	{"exit", exitProgram, 0, 0, 0 },
	{"crash", crash, 0, 0, 0 },
	{"forceGC", forceGC, 0, 0, 0 },
	{"vmem", vmem, 0, 0, 0 },
	{"_rewriteMaps", _rewriteMaps, 0, 0, 0 },
	{"_lodbias", _lodbias, 0, 0, 0 },
	{0, 0, 0, 0, 0}
};

enum ScriptGlobalTinyIDs
{
	GLOBAL_SELECTION,
	GLOBAL_GROUPSARRAY,
	GLOBAL_CAMERA,
	GLOBAL_CONSOLE,
};

JSPropertySpec ScriptGlobalTable[] =
{
	{ "selection", GLOBAL_SELECTION, JSPROP_PERMANENT, JSI_Selection::getSelection, JSI_Selection::setSelection },
	{ "groups", GLOBAL_GROUPSARRAY, JSPROP_PERMANENT, JSI_Selection::getGroups, JSI_Selection::setGroups },
	{ "camera", GLOBAL_CAMERA, JSPROP_PERMANENT, JSI_Camera::getCamera, JSI_Camera::setCamera },
	{ "console", GLOBAL_CONSOLE, JSPROP_PERMANENT | JSPROP_READONLY, JSI_Console::getConsole, NULL },
	{ "entities", 0, JSPROP_PERMANENT | JSPROP_READONLY, GetEntitySet, NULL },
	{ "players", 0, JSPROP_PERMANENT | JSPROP_READONLY, GetPlayerSet, NULL },
	{ "localPlayer", 0, JSPROP_PERMANENT, GetLocalPlayer, SetLocalPlayer },
	{ "gaiaPlayer", 0, JSPROP_PERMANENT | JSPROP_READONLY, GetGaiaPlayer, NULL },
	{ "gameView", 0, JSPROP_PERMANENT | JSPROP_READONLY, GetGameView, NULL },
	{ 0, 0, 0, 0, 0 },
};

// Allow scripts to output to the global log file
JSBool WriteLog(JSContext* context, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* UNUSEDPARAM(rval))
{
	if (argc < 1)
		return JS_FALSE;

	CStr logMessage;

	for (int i = 0; i < (int)argc; i++)
	{
		try
		{
			CStr arg = g_ScriptingHost.ValueToString( argv[i] );
			logMessage += arg;
		}
		catch( PSERROR_Scripting_ConversionFailed )
		{
			// Do nothing.
		}
	}

	// We should perhaps unicodify (?) the logger at some point.

	LOG( NORMAL, "script", logMessage );

	return JS_TRUE;
}

JSBool getEntityByHandle( JSContext* context, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval )
{
	debug_assert( argc >= 1 );
	i32 handle;
	try
	{
		handle = g_ScriptingHost.ValueToInt( argv[0] );
	}
	catch( PSERROR_Scripting_ConversionFailed )
	{
		*rval = JSVAL_NULL;
		JS_ReportError( context, "Invalid handle" );
		return( JS_TRUE );
	}
	HEntity* v = g_EntityManager.getByHandle( (u16)handle );
	if( !v )
	{
		JS_ReportError( context, "No entity occupying handle: %d", handle );
		*rval = JSVAL_NULL;
		return( JS_TRUE );
	}
	JSObject* entity = (*v)->GetScript();

	*rval = OBJECT_TO_JSVAL( entity );
	return( JS_TRUE );
}

JSBool getEntityTemplate( JSContext* context, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval )
{
	debug_assert( argc >= 1 );
	CStrW templateName;
	try
	{
		templateName = g_ScriptingHost.ValueToUCString( argv[0] );
	}
	catch( PSERROR_Scripting_ConversionFailed )
	{
		*rval = JSVAL_NULL;
		JS_ReportError( context, "Invalid template identifier" );
		return( JS_TRUE );
	}
	CBaseEntity* v = g_EntityTemplateCollection.getTemplate( templateName );
	if( !v )
	{
		*rval = JSVAL_NULL;
		JS_ReportError( context, "No such template: %s", CStr8(templateName).c_str() );
		return( JS_TRUE );
	}
	
	*rval = OBJECT_TO_JSVAL( v->GetScript() );

	return( JS_TRUE );
}

JSBool GetEntitySet( JSContext* context, JSObject* globalObject, jsval argv, jsval* vp )
{
	std::vector<HEntity>* extant = g_EntityManager.getExtant();

	*vp = OBJECT_TO_JSVAL( EntityCollection::Create( *extant ) );
	delete( extant );

	return( JS_TRUE );
}

JSBool GetPlayerSet( JSContext* cx, JSObject* globalObject, jsval id, jsval* vp )
{
	std::vector<CPlayer*>* players = g_Game->GetPlayers();

	*vp = OBJECT_TO_JSVAL( PlayerCollection::Create( *players ) );

	return( JS_TRUE );
}

JSBool GetLocalPlayer( JSContext* cx, JSObject* globalObject, jsval id, jsval* vp )
{
	*vp = OBJECT_TO_JSVAL( g_Game->GetLocalPlayer()->GetScript() );
	return( JS_TRUE );
}

JSBool GetGaiaPlayer( JSContext* cx, JSObject* globalObject, jsval id, jsval* vp )
{
	*vp = OBJECT_TO_JSVAL( g_Game->GetPlayer( 0 )->GetScript() );
	return( JS_TRUE );
}

JSBool SetLocalPlayer( JSContext* context, JSObject* obj, jsval id, jsval* vp )
{
	CPlayer* newLocalPlayer = ToNative<CPlayer>( *vp );

	if( !newLocalPlayer )
	{
		JS_ReportError( context, "Not a valid Player." );
		return( JS_TRUE );
	}

	g_Game->SetLocalPlayer( newLocalPlayer );
	return( JS_TRUE );
}

JSBool GetGameView( JSContext* cx, JSObject* globalObject, jsval id, jsval* vp )
{
	if (g_Game)
		*vp = OBJECT_TO_JSVAL( g_Game->GetView()->GetScript() );
	else
		*vp = JSVAL_NULL;
	return( JS_TRUE );
}

JSBool AddGlobalHandler( JSContext* cx, JSObject* obj, unsigned int argc, jsval* argv, jsval* rval )
{
	*rval = BOOLEAN_TO_JSVAL( g_JSGameEvents.AddHandlerJS( cx, argc, argv ) );
	return( JS_TRUE );
}

JSBool RemoveGlobalHandler( JSContext* cx, JSObject* obj, unsigned int argc, jsval* argv, jsval* rval )
{
	*rval = BOOLEAN_TO_JSVAL( g_JSGameEvents.RemoveHandlerJS( cx, argc, argv ) );
	return( JS_TRUE );
}

JSBool setCameraTarget( JSContext* context, JSObject* obj, unsigned int argc, jsval* argv, jsval* rval )
{
	CVector3D* target; 
	if( ( argc == 0 ) || !( target = ToNative<CVector3D>( argv[0] ) ) )
	{
		JS_ReportError( context, "Invalid camera target" );
		return( JS_TRUE );
	}
	g_Game->GetView()->SetCameraTarget( *target );

	*rval = JSVAL_TRUE;
	return( JS_TRUE );
}

JSBool setTimeout( JSContext* context, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* UNUSEDPARAM(rval) )
{
	debug_assert( argc >= 2 );
	size_t delay;
	try
	{
		delay = g_ScriptingHost.ValueToInt( argv[1] );
	}
	catch( ... )
	{
		JS_ReportError( context, "Invalid timer parameters" );
		return( JS_TRUE );
	}

	switch( JS_TypeOfValue( context, argv[0] ) )
	{
	case JSTYPE_STRING:
	{
		CStrW fragment = g_ScriptingHost.ValueToUCString( argv[0] );
		g_Scheduler.pushTime( delay, fragment, JS_GetScopeChain( context ) );
		return( JS_TRUE );
	}
	case JSTYPE_FUNCTION:
	{
		JSFunction* fn = JS_ValueToFunction( context, argv[0] );
		g_Scheduler.pushTime( delay, fn, JS_GetScopeChain( context ) );
		return( JS_TRUE );
	}
	default:
		JS_ReportError( context, "Invalid timer script" );
		return( JS_TRUE );
	}
}

JSBool setInterval( JSContext* context, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* UNUSEDPARAM(rval) )
{
	debug_assert( argc >= 2 );
	size_t first, interval;
	try
	{
		first = g_ScriptingHost.ValueToInt( argv[1] );
		if( argc == 3 )
		{
			// toDo, first, interval
			interval = g_ScriptingHost.ValueToInt( argv[2] );
		}
		else
		{
			// toDo, interval (first = interval)
			interval = first;
		}
	}
	catch( ... )
	{
		JS_ReportError( context, "Invalid timer parameters" );
		return( JS_TRUE );
	}

	switch( JS_TypeOfValue( context, argv[0] ) )
	{
	case JSTYPE_STRING:
	{
		CStrW fragment = g_ScriptingHost.ValueToUCString( argv[0] );
		g_Scheduler.pushInterval( first, interval, fragment, JS_GetScopeChain( context ) );
		return( JS_TRUE );
	}
	case JSTYPE_FUNCTION:
	{
		JSFunction* fn = JS_ValueToFunction( context, argv[0] );
		g_Scheduler.pushInterval( first, interval, fn, JS_GetScopeChain( context ) );
		return( JS_TRUE );
	}
	default:
		JS_ReportError( context, "Invalid timer script" );
		return( JS_TRUE );
	}
}

JSBool cancelInterval( JSContext* UNUSEDPARAM(context), JSObject* UNUSEDPARAM(globalObject), unsigned int UNUSEDPARAM(argc), jsval* UNUSEDPARAM(argv), jsval* UNUSEDPARAM(rval) )
{
	g_Scheduler.m_abortInterval = true;
	return( JS_TRUE );
}

JSBool v3dist( JSContext* cx, JSObject* obj, unsigned int argc, jsval* argv, jsval* rval )
{
	debug_assert( argc >= 2 );
	CVector3D* a = ToNative<CVector3D>( argv[0] );
	CVector3D* b = ToNative<CVector3D>( argv[1] );
	float dist = ( *a - *b ).GetLength();
	*rval = ToJSVal( dist );
	return( JS_TRUE );
}

JSBool forceGC( JSContext* cx, JSObject* obj, unsigned int argc, jsval* argv, jsval* rval )
{
	double time = get_time();
	JS_GC( cx );
	time = get_time() - time;
	g_Console->InsertMessage( L"Garbage collection completed in: %f", time );
	*rval = JSVAL_TRUE;
	return( JS_TRUE );
}

JSBool getGUIGlobal( JSContext* UNUSEDPARAM(context), JSObject* UNUSEDPARAM(globalObject), unsigned int UNUSEDPARAM(argc), jsval* UNUSEDPARAM(argv), jsval* rval )
{
	*rval = OBJECT_TO_JSVAL( g_GUI.GetScriptObject() );
	return( JS_TRUE );
}

JSBool getGlobal( JSContext* UNUSEDPARAM(context), JSObject* globalObject, unsigned int UNUSEDPARAM(argc), jsval* UNUSEDPARAM(argv), jsval* rval )
{
	*rval = OBJECT_TO_JSVAL( globalObject );
	return( JS_TRUE );
}


extern CStr g_CursorName; // from main.cpp
JSBool setCursor(JSContext* UNUSEDPARAM(context), JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* UNUSEDPARAM(rval))
{
	if (argc != 1)
	{
		debug_assert(! "Invalid parameter count to setCursor");
		return JS_FALSE;
	}
	g_CursorName = g_ScriptingHost.ValueToString(argv[0]);
	return JS_TRUE;
}

JSBool createServer(JSContext* cx, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval)
{
	if (!g_Game)
		g_Game=new CGame();
	if (!g_NetServer)
		g_NetServer=new CNetServer(g_Game, &g_GameAttributes);
	
	*rval=OBJECT_TO_JSVAL(g_NetServer->GetScript());
	return JS_TRUE;
}

// from main.cpp
extern void StartGame();

JSBool createClient(JSContext* cx, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval)
{
	if (!g_Game)
		g_Game=new CGame();
	if (!g_NetClient)
		g_NetClient=new CNetClient(g_Game, &g_GameAttributes);
	
	*rval=OBJECT_TO_JSVAL(g_NetClient->GetScript());
	return JS_TRUE;
}

// TODO Replace startGame with create(Game|Server|Client)/game.start() - after
// merging CGame and CGameAttributes
JSBool startGame(JSContext* cx, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval)
{
	if (argc != 0)
	{
		return JS_FALSE;
	}

	*rval=BOOLEAN_TO_JSVAL(JS_TRUE);

	// Hosted MP Game
	if (g_NetServer)
		*rval=BOOLEAN_TO_JSVAL(g_NetServer->StartGame() == 0);
	// Joined MP Game: startGame is invalid - do nothing
	else if (g_NetClient)
	{
	}
	// Start an SP Game Session
	else if (!g_Game)
	{
		g_Game=new CGame();
		PSRETURN ret = g_Game->StartGame(&g_GameAttributes);
		if (ret != PSRETURN_OK)
		{
			// Failed to start the game - destroy it, and return false

			delete g_Game;
			g_Game = NULL;

			*rval=BOOLEAN_TO_JSVAL(JS_FALSE);
			return JS_TRUE;
		}
	}

	return JS_TRUE;
}

extern void EndGame();
JSBool endGame(JSContext* UNUSEDPARAM(context), JSObject* UNUSEDPARAM(globalObject), unsigned int UNUSEDPARAM(argc), jsval* UNUSEDPARAM(argv), jsval* UNUSEDPARAM(rval))
{
	EndGame();
	return JS_TRUE;
}


JSBool loadLanguage(JSContext* cx, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval)
{
	if (argc != 1)
	{
		JS_ReportError(cx, "loadLanguage: needs 1 parameter");
		return JS_FALSE;
	}
	CStr lang = g_ScriptingHost.ValueToString(argv[0]);
	I18n::LoadLanguage(lang);

	return JS_TRUE;
}

JSBool getLanguageID(JSContext* cx, JSObject* UNUSEDPARAM(globalObject), unsigned int UNUSEDPARAM(argc), jsval* UNUSEDPARAM(argv), jsval* rval)
{
	JSString* s = JS_NewStringCopyZ(cx, I18n::CurrentLanguageName());
	if (!s)
	{
		JS_ReportError(cx, "Error creating string");
		return JS_FALSE;
	}
	*rval = STRING_TO_JSVAL(s);
	return JS_TRUE;
}

extern "C" int fps;
JSBool getFPS(JSContext* UNUSEDPARAM(cx), JSObject* UNUSEDPARAM(globalObject), unsigned int UNUSEDPARAM(argc), jsval* UNUSEDPARAM(argv), jsval* rval)
{
	*rval = INT_TO_JSVAL(fps);
	return JS_TRUE;
}

JSBool buildTime(JSContext* context, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval)
{
	if (argc > 1)
	{
		JS_ReportError(context, "buildTime: needs 0 or 1 parameter");
		return JS_FALSE;
	}
	// buildTime( ) = "date time"
	// buildTime(0) = "date"
	// buildTime(1) = "time"
	JSString* s = JS_NewStringCopyZ(context,
		argc && argv[0]==JSVAL_ONE ? __TIME__
	  :	argc ? __DATE__
	  : __DATE__" "__TIME__
	);
	*rval = STRING_TO_JSVAL(s);
	return JS_TRUE;
}



extern void kill_mainloop(); // from main.cpp

JSBool exitProgram(JSContext* context, JSObject* globalObject, unsigned int argc, jsval* UNUSEDPARAM(argv), jsval* UNUSEDPARAM(rval))
{
	kill_mainloop();
	return JS_TRUE;
}

JSBool crash(JSContext* context, JSObject* globalObject, unsigned int argc, jsval* argv, jsval* rval)
{
	MICROLOG(L"Crashing at user's request.");
	uintptr_t ptr = 0xDEADC0DE; // oh dear, might this be an invalid pointer?
	return *(JSBool*) ptr;
}

JSBool vmem(JSContext* context, JSObject* globalObject, unsigned int argc, jsval* argv, jsval* rval)
{
#ifdef _WIN32
	int left, total;
	extern int GetVRAMInfo(int&, int&);
	if (GetVRAMInfo(left, total))
		g_Console->InsertMessage(L"VRAM: used %d, total %d, free %d", total-left, total, left);
	else
		g_Console->InsertMessage(L"VRAM: failed to detect");
#else
	g_Console->InsertMessage(L"VRAM: [not available on non-Windows]");
#endif
	return JS_TRUE;
}

JSBool _rewriteMaps(JSContext* context, JSObject* globalObject, unsigned int argc, jsval* argv, jsval* rval)
{
	extern CLightEnv g_LightEnv;
	CMapWriter::RewriteAllMaps(g_Game->GetWorld()->GetTerrain(), g_Game->GetWorld()->GetUnitManager(), &g_LightEnv);
	return JS_TRUE;
}

#include "Renderer.h"
JSBool _lodbias(JSContext* context, JSObject* globalObject, unsigned int argc, jsval* argv, jsval* rval)
{
	g_Renderer.SetOptionFloat(CRenderer::OPT_LODBIAS, (float)g_ScriptingHost.ValueToDouble(argv[0]));
	return JS_TRUE;
}

JSBool issueCommand(JSContext* context, JSObject* UNUSEDPARAM(globalObject), unsigned int argc, jsval* argv, jsval* rval)
{
	debug_assert(argc >= 2);
	debug_assert(JSVAL_IS_OBJECT(argv[0]));
	
	CEntityList entities;

	if (JS_GetClass(JSVAL_TO_OBJECT(argv[0])) == &CEntity::JSI_class)
		entities.push_back( (ToNative<CEntity>(argv[0])) ->me);
	else	
		entities = *EntityCollection::RetrieveSet(context, JSVAL_TO_OBJECT(argv[0]));

	CNetMessage *msg=CNetMessage::CommandFromJSArgs(entities, context, argc-1, argv+1);
	if (msg)
	{
		g_Console->InsertMessage(L"issueCommand: %hs", msg->GetString().c_str());
		*rval = g_ScriptingHost.UCStringToValue(msg->GetString());
		
		g_Game->GetSimulation()->QueueLocalCommand(msg);
	}
	else
	{
		*rval = JSVAL_FALSE;
	}
	return JS_TRUE;
}

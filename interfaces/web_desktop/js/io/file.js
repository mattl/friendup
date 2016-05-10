/*******************************************************************************
*                                                                              *
* This file is part of FRIEND UNIFYING PLATFORM.                               *
*                                                                              *
* This program is free software: you can redistribute it and/or modify         *
* it under the terms of the GNU Affero General Public License as published by  *
* the Free Software Foundation, either version 3 of the License, or            *
* (at your option) any later version.                                          *
*                                                                              *
* This program is distributed in the hope that it will be useful,              *
* but WITHOUT ANY WARRANTY; without even the implied warranty of               *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
* GNU Affero General Public License for more details.                          *
*                                                                              *
* You should have received a copy of the GNU Affero General Public License     *
* along with this program.  If not, see <http://www.gnu.org/licenses/>.        *
*                                                                              *
*******************************************************************************/

var friendUP = window.friendUP;
friendUP.io = friendUP.io || {};

// Simple file class
File = function ( filename )
{
	this.data = false;
	this.rawdata = false;
	this.replacements = false;
	this.vars = {};
	
	// Add a var
	this.addVar = function( k, v )
	{
		this.vars[k] = v;
	}
	
	this.resolvePath = function( filename )
	{
		if( filename.toLowerCase().substr( 0, 8 ) == 'progdir:' )
		{
			filename = filename.substr( 8, filename.length - 8 );
			if( this.application && this.application.filePath )
				filename = this.application.filePath + filename;
		}
		// TODO: Remove system: here (we're rollin with Libs:)
		else if( 
			filename.toLowerCase().substr( 0, 7 ) == 'system:' ||
			filename.toLowerCase().substr( 0, 4 ) == 'libs:' 
		)
		{
			filename = filename.substr( 7, filename.length - 7 );
			filename = '/webclient/' + filename;
		}
		// Fix broken paths
		if( filename.substr( 0, 20 ) == 'resources/webclient/' )
			filename = filename.substr( 20, filename.length - 20 );
			
		return filename;
	}
	
	// Load data
	this.load = function()
	{
		var t = this;
		var jax = new cAjax ();
		
		for( var a in this.vars )
			jax.addVar( a, this.vars[a] );
		
		// Check progdir on path
		if( filename )
		{
			filename = this.resolvePath( filename );
		}
		
		// Get the correct door and load data
		var theDoor = Doors.getDoorByPath( filename );
		if( theDoor )
		{
			// Copy vars
			for( var a in this.vars )
			{
				theDoor.addVar( a, this.vars[a] );
			}
			
			theDoor.onRead = function( data )
			{
				if( t.replacements )
				{
					for( var a in t.replacements )
						data = data.split ( '{'+a+'}' ).join ( t.replacements[a] );
				}
				if( typeof ( t.onLoad ) != 'undefined' )
				{
					t.onLoad( data );
				}
			}
			theDoor.read( filename );
		}
		// Old fallback (should never happen)
		else
		{
			jax.open( 'post', filename, true, true );	
		
			//console.log('PATH ' + filename );
			// File description
			if ( typeof( filename ) == 'string' )
			{
				jax.addVar( 'path', filename );
			}
			else
			{
				jax.addVar( 'fileInfo', JSON.stringify ( jsonSafeObject ( filename ) ) );
			}
			jax.addVar( 'sessionid', Doors.sessionId );
		
			jax.onload = function()
			{
				t.data = false;
				t.rawdata = false;
				if( this.returnCode == 'ok' )
				{
					try{ t.data = decodeURIComponent( this.returnData ); }
					catch( e ){ t.data = this.returnData; }
				
					if( t.replacements )
					{
						for( var a in t.replacements )
							t.data = t.data.split ( '{'+a+'}' ).join ( t.replacements[a] );
					}
					if( typeof ( t.onLoad ) != 'undefined' )
					{
						t.onLoad( t.data );
					}
				}
				// Load the raw data
				else if( ( !this.returnCode || this.returnCode.length > 3 ) && this.responseText().length )
				{
					t.rawdata = this.responseText();
					if ( typeof( t.onLoad ) != 'undefined' )
					{
						if( t.replacements )
						{
							for( var a in t.replacements )
								t.rawdata = t.rawdata.split ( '{'+a+'}' ).join ( t.replacements[a] );
						}
						t.onLoad( t.rawdata );
					}
				}
				// Catch all (also raw data)
				else
				{
					t.rawdata = this.responseText();
					if ( typeof( t.onLoad ) != 'undefined' )
					{
						if( t.replacements )
						{
							for( var a in t.replacements )
								t.rawdata = t.rawdata.split ( '{'+a+'}' ).join ( t.replacements[a] );
						}
						t.onLoad( t.rawdata );
					}
				}
			}
			jax.send ();
		}
	}
	
	// Posts a file of filename to a destination path (including content)
	// filePath = the name of the file (full path)
	// content = the data stream
	// callback = the function to call when we finished up
	this.post = function( filePath, content )
	{
		var t = this;
		if( filePath && content )
		{
			var files = [ content ];
			
			var uworker = new Worker( 'js/io/filetransfer.js' );
			
			// Open window
			var w = new View( { 
				title:  i18n( 'i18n_copying_files' ), 
				width:  320, 
				height: 100
			} );
			
			var uprogress = new File( 'templates/file_operation.html' );

			uprogress.connectedworker = uworker;
			
			uprogress.onLoad = function( data )
			{
				data = data.split( '{cancel}' ).join( i18n( 'i18n_cancel' ) );
				w.setContent( data );
			
				w.connectedworker = this.connectedworker;
				w.onClose = function()
				{
					Workspace.refreshWindowByPath( filePath );
					if( this.connectedworker )
					{
						this.connectedworker.postMessage( { 'terminate': 1 } );
					}
				}
			
				uprogress.myview = w;
			
				// Setup progress bar
				var eled = w.getWindowElement().getElementsByTagName( 'div' );
				var groove = false, bar = false, frame = false, progressbar = false;
				for( var a = 0; a < eled.length; a++ )
				{
					if( eled[a].className )
					{
						var types = [ 'ProgressBar', 'Groove', 'Frame', 'Bar', 'Info' ];
						for( var b = 0; b < types.length; b++ )
						{
							if( eled[a].className.indexOf( types[b] ) == 0 )
							{
								switch( types[b] )
								{
									case 'ProgressBar': progressbar    = eled[a]; break;
									case 'Groove':      groove         = eled[a]; break;
									case 'Frame':       frame          = eled[a]; break;
									case 'Bar':         bar            = eled[a]; break;
									case 'Info':		uprogress.info = eled[a]; break;
								}
								break;
							}
						}
					}
				}
				
				
				//activate cancel button... we assume we only hav eone button in the template
				var cb = w.getWindowElement().getElementsByTagName( 'button' )[0];
				
				cb.mywindow = w;
				cb.onclick = function( e )
				{
					this.mywindow.close();
				}
				
				// Only continue if we have everything
				if( progressbar && groove && frame && bar )
				{
					progressbar.style.position = 'relative';
					frame.style.width = '100%';
					frame.style.height = '40px';
					groove.style.position = 'absolute';
					groove.style.width = '100%';
					groove.style.height = '30px';
					groove.style.top = '0';
					groove.style.left = '0';
					bar.style.position = 'absolute';
					bar.style.width = '2px';
					bar.style.height = '30px';
					bar.style.top = '0';
					bar.style.left = '0';
					
					// Preliminary progress bar
					bar.total = files.length;
					bar.items = files.length;
					uprogress.bar = bar;
				}
				uprogress.loaded = true;
				uprogress.setProgress(0);
			}
			
			uprogress.setProgress = function( percent )
			{
				// only update display if we are loaded...
				// otherwise just drop and wait for next call to happen ;)
				if( uprogress.loaded )
				{
					uprogress.bar.style.width = Math.floor( Math.max(1,percent ) ) + '%';
					uprogress.bar.innerHTML = '<div class="FullWidth" style="text-overflow: ellipsis; text-align: center; line-height: 30px; color: white">' +
					Math.floor( percent ) + '%</div>';
				}
			};
			
			uprogress.setUnderTransport = function()
			{
				// show notice that we are transporting files to the server....
				uprogress.info.innerHTML = '<div id="transfernotice" style="padding-top:10px;">Transferring files to target volume...</div>';
				uprogress.myview.setFlag("height",125);
			}
			
			uprogress.displayError = function( msg )
			{
				uprogress.info.innerHTML = '<div style="color:#F00; padding-top:10px; font-weight:700;">'+ msg +'</div>';
				uprogress.myview.setFlag("height",140);
			}
			
			uworker.onerror = function( err )
			{
				console.log('Upload worker error #######');
				console.log( err );
				console.log('###########################');	
			}
			
			uworker.onmessage = function( e )
			{
				//console.log('Worker sends us back ------------ -');
				//console.log( e.data );
				//console.log('--------------------------------- -');
				if( e.data['progressinfo'] == 1 )
				{
					if( e.data['uploadscomplete'] == 1 )
					{
						w.close();
						Workspace.refreshWindowByPath( filePath );
						if( t.onPost )
						{
							t.onPost( true );
						}
						return true;
					}
					else if( e.data['progress'] )
					{
						uprogress.setProgress( e.data['progress'] );
						if( e.data['filesundertransport'] && e.data['filesundertransport'] > 0 )
						{
							uprogress.setUnderTransport();
						}
					}
				}
				else if( e.data['error'] == 1 )
				{
					uprogress.displayError(e.data['errormessage']);
				}
			}
			
			uprogress.load();
			
			// Do the hustle!
			var vol = filePath.split( ':' )[0];
			var path = filePath;
			uworker.postMessage( {
				'session': Workspace.sessionId,
				'targetPath': path, 
				'targetVolume': vol,
				'objectdata': Base64.encode( content )
			} );
			console.log( 'Execute: ' );
			console.log( path, vol );
			
		}
	}
	
	// Save data to a file
	this.save = function ( filename, content )
	{
		t = this;
		// Get the correct door and load data
		var theDoor = Doors.getDoorByPath( filename );
		if( theDoor )
		{
			// Copy vars
			for( var a in this.vars ) 
				theDoor.addVar( a, this.vars[a] );
			
			theDoor.onWrite = function( data )
			{
				if( typeof ( t.onSave ) != 'undefined' )
					t.onSave( data );
			}
			theDoor.write( filename, content );
		}
		// Old fallback (should never happen)
		else
		{
			var jax = new cAjax();
			
			jax.open ( 'post', '/system.library', true, true );
			
			for( var a in this.vars )
			{
				console.log( 'Adding extra var ' + a, this.vars[a] );
				jax.addVar( a, this.vars[a] );
			}
				
			jax.addVar( 'sessionId', Doors.sessionId );
			jax.addVar( 'module', 'system' );
			jax.addVar( 'command', 'filesave' );
			jax.addVar( 'path', filename );
			jax.addVar( 'mode', 'save' );
			jax.addVar( 'content', content );
			jax.t = this;
			jax.onload = function ()
			{
				if ( this.returnCode == 'ok' )
				{
					this.t.written = parseInt ( this.returnData );
					if ( typeof ( this.t.onSave ) != 'undefined' )
					{
						this.t.onSave ();
					}
				}
				else 
				{
					this.written = 0;
				}
			}
			jax.send ();
		}
	}
};


// File
// same as above, but different interface
// also, probably not working yet
(function ( ns, undefined ) {
	ns.File = function( conf, callback ) {
		if ( !( this instanceof ns.File ))
			return new ns.File( conf, callback );
		
		var self = this;
		self.filePath = conf.filePath;
		self.callback = callback;
		
		self.init();
	}
	
	ns.File.prototype.init = function()
	{
		var self = this;
		var request = new friendUP.io.Request({
			url : self.filePath,
			args : {
				path : self.filePath
			},
			success : success,
			error : error
		});
		
		function success( response ) { self.done( response.data ); }
		function error( e ) { self.done( e ); }
	}
	
	ns.File.prototype.done = function( data )
	{
		var self = this;
		
		if ( !data ) 
		{
			data = null;
		}
		
		self.callback( data );
	}
	
})( friendUP.io );

// Resolve an image on the global level
// TODO: Use Door to resolve proper path
function getImageUrl( path )
{
	var u = '/system.library/file/read?sessionid=' + Doors.sessionId + '&path=' + path + '&mode=rb';
	return u;
}


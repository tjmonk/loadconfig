/*============================================================================
MIT License

Copyright (c) 2023 Trevor Monk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
============================================================================*/

/*!
 * @defgroup loadconfig loadconfig
 * @brief Confguration Management Utility to Load system variables
 * @{
 */

/*==========================================================================*/
/*!
@file loadconfig.c

    Load Variables

    The loadconfig utility manages system configuration by loading variable
    data across one or more files.

    The loadconfig utility processes data from configuration files one line
    at a time.

    Each line in a configuration file may be an @ directive, or
    a var/value assignment.

    Lines begining with @ are directives

    Lines begining with # are comments

    Blank lines are ignored

    All other lines are assumed to be var/value pairs

    Every configuration file MUST begin with the @config directive

    An example configuration file is shown below:

        @config Main system configuration

        # The main system configuration file is the configuration entry point
        # and includes all other configurations

        @include software.cfg
        @require hardware.cfg

        /sys/network/hostname  MyHostName
        /sys/network/dhcp      1
        /sys/network/ntp       0


    The following directives are supported:

    @config - specifies the description of the configuration file and
              :must be present in the first line of every configuration file

    @require - specifies another (mandatory) configuration file to process

    @include - specifies another (optional) configuration file to process

    @includedir - specifies a directory of configuration files to process


*/
/*==========================================================================*/

/*============================================================================
        Includes
============================================================================*/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <dirent.h>
#include <varserver/vartemplate.h>
#include <varserver/varserver.h>

/*============================================================================
        Private definitions
============================================================================*/

/*! configuration tag */
#define CONFIG_TAG  "@config"

/*! default working buffer size */
#define DEFAULT_WORKBUF_SIZE    ( BUFSIZ )

/*! size of the buffer used to store the shared memory client name */
#define CLIENT_NAME_SIZE ( 128 )

/*! Load state */
typedef struct loadState
{
    /*! variable server handle */
    VARSERVER_HANDLE hVarServer;

    /*! verbose flag */
    bool verbose;

    /*! name of the configuration file */
    char *pFileName;

    /*! current line number of the active configuration file */
    int lineno;

    /*! required flag indicating if the config file is mandatory */
    bool required;

    /*! working buffer file descriptor */
    int fd;

    /*! working buffer size */
    int workbufSize;

    /*! pointer to the working buffer */
    char *workbuf;

    /*! shared memory client name */
    char clientname[CLIENT_NAME_SIZE];

} LoadState;

/*============================================================================
        Private file scoped variables
============================================================================*/

/*============================================================================
        Private function declarations
============================================================================*/

void main(int argc, char **argv);
static int ProcessOptions( int argC, char *argV[], LoadState *pState );
static void usage( char *cmdname );
static int CreateWorkingBuffer( LoadState *pState );
static void DestroyWorkingBuffer( LoadState *pState );
static int ProcessConfigFile( LoadState *pState, char *filename );
static int ProcessConfigData( LoadState *pState, char *pConfigData );
static int ProcessConfigLine( LoadState *pState, char *pConfigLine );
static int ProcessDirective( LoadState *pState, char *pConfigDirective );
static int ProcessConfigDirective( LoadState *pState, char *pInfo );
static int ProcessIncludeDirective( LoadState *pState, char *pFilename );
static int ProcessRequireDirective( LoadState *pState, char *pFilename );
static int ProcessIncludeDirDirective( LoadState *pState, char *pDirname );
static int ProcessVariableAssignment( LoadState *pState, char *pConfig );
void LogError( LoadState *pState, char *error );
static char *GetConfigData( char *filename );
static size_t GetFileSize( char *filename );
static bool IsConfigFile( FILE *fp );
static char *ReadConfigData( FILE *fp, size_t n );

/*============================================================================
        Private function definitions
============================================================================*/

/*==========================================================================*/
/*  main                                                                    */
/*!
    Main entry point for the loadconfig application

    The main function starts the loadconfig application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

============================================================================*/
void main(int argc, char **argv)
{
    LoadState state;

    /* clear the load state object */
    memset( &state, 0, sizeof( state ) );

    /* initialize the load state object */
    state.fd = -1;
    state.workbufSize = DEFAULT_WORKBUF_SIZE;

    if( argc < 2 )
    {
        usage( argv[0] );
        exit( 1 );
    }

    /* process the command line options */
    ProcessOptions( argc, argv, &state );

    /* open a handle to the variable server */
    state.hVarServer = VARSERVER_Open();
    if( state.hVarServer != NULL )
    {
        if ( CreateWorkingBuffer(&state) == EOK )
        {
            /* indicate that the top level config file is mandatory */
            state.required = true;

            /* Process the configuration file */
            ProcessConfigFile( &state, state.pFileName );

            /*! destroy the working buffer */
            DestroyWorkingBuffer(&state);
        }
        else
        {
            LogError( &state, "Cannot create working buffer" );
        }

        /* close the handle to the variable server */
        VARSERVER_Close( state.hVarServer );
    }
}

/*==========================================================================*/
/*  usage                                                                   */
/*!
    Display the application usage

    The usage function dumps the application usage message
    to stderr.

    @param[in]
       cmdname
            pointer to the invoked command name

    @return none

============================================================================*/
static void usage( char *cmdname )
{
    if( cmdname != NULL )
    {
        fprintf(stderr,
                "usage: %s [-v] [-h]\n"
                " [-h] : display this help\n"
                " [-v] : verbose output\n"
                " [-W <size> ] : working buffer size\n"
                " -f <filename> : configuration file\n",
                cmdname );
    }
}

/*==========================================================================*/
/*  ProcessOptions                                                          */
/*!
    Process the command line options

    The ProcessOptions function processes the command line options and
    populates the ExecVarState object

    @param[in]
        argC
            number of arguments
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pState
            pointer to the loadconfig utility state object

    @return 0

============================================================================*/
static int ProcessOptions( int argC, char *argV[], LoadState *pState )
{
    int c;
    int result = EINVAL;
    const char *options = "hvf:w:";

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'v':
                    pState->verbose = true;
                    break;

                case 'h':
                    usage( argV[0] );
                    break;

                case 'f':
                    pState->pFileName = strdup(optarg);
                    break;

                case 'w':
                    pState->workbufSize = atol(optarg);
                    break;

                default:
                    break;

            }
        }
    }

    return 0;
}

/*==========================================================================*/
/*  CreateWorkingBuffer                                                     */
/*!
    Create a working buffer

    The CreateWorkingBuffer function creates a working buffer that can
    be used to expand configuration lines which contain system variables
    to be expanded.

    @param[in]
        pState
            pointer to the loadconfig utility state object

    @retval EOK working buffer created ok
    @retval EINVAL invalid arguments
    @retval other error as returned by shm_open, ftruncate, mmap

============================================================================*/
static int CreateWorkingBuffer( LoadState *pState )
{
    int result = EINVAL;
	pid_t pid;
    size_t size;
    int fd;
    int rc;
    char *workbuf;

    if ( ( pState != NULL ) &&
         ( pState->workbufSize > 0 ) )
    {
        /* build the varclient identifier */
        pid = getpid();
        sprintf(pState->clientname, "/load_%d", pid);

        /* set the working buffer size including space for an
         * additional NUL terminator */
        size = pState->workbufSize + 1;

        /* get shared memory file descriptor (NOT a file) */
        fd = shm_open(pState->clientname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd != -1)
        {
            /* extend shared memory object as by default
               it is initialized with size 0 */
            rc = ftruncate(fd, size );
            if (rc != -1)
            {
                /* map shared memory to process address space */
                workbuf = mmap( NULL,
                                size ,
                                PROT_WRITE,
                                MAP_SHARED,
                                fd,
                                0);

                if( workbuf != MAP_FAILED )
                {
                    /* populate the VarClient object */
                    pState->workbuf = workbuf;
                    pState->fd = fd;

                    /* clear the working buffer */
                    memset( workbuf, 0, size );

                    result = EOK;
                }
                else
                {
                    /* memory map failed */
                    pState->fd = -1;
                    pState->workbuf = NULL;
                    close( fd );
                    result = errno;
                }
            }
            else
            {
                pState->fd = -1;
                pState->workbuf = NULL;
                close( fd );
                result = errno;
            }
        }
        else
        {
            pState->fd = -1;
            pState->workbuf = NULL;
            result = errno;
        }
    }

    if ( result != EOK )
    {
        fprintf(stderr, "CreateWorkingBuffer: %s\n", strerror( result ) );
    }

    return result;
}

/*==========================================================================*/
/*  DestroyWorkingBuffer                                                     */
/*!
    Destroy the working buffer

    The DestroyWorkingBuffer function removes the shared memory and file
    descriptor used for processing lines of configuration data.

    @param[in]
        pState
            pointer to the loadconfig utility state object

============================================================================*/
static void DestroyWorkingBuffer( LoadState *pState )
{
    if ( pState != NULL )
    {
        if ( pState->workbuf != NULL )
        {
            /* Unmap the shared memory object from the virtual
             * address space of the loadconfig application */
            munmap( pState->workbuf, pState->workbufSize );
            pState->workbuf = NULL;
        }

        if ( pState->fd != -1 )
        {
            /* close the shared memory file descriptor */
            close( pState->fd );
        }

        /* unlink the shared memory object name */
        shm_unlink( pState->clientname );
    }
}

/*==========================================================================*/
/*  ProcessConfigFile                                                       */
/*!
    Process the specified configuration file

    The ProcessConfigFile function processes a configuration file
    consisting of lines of directives and variable assignments.


    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        filename
            pointer to the name of the file to load

    @retval EINVAL invalid arguments
    @retval EOK file processed ok
    @retval other error as returned by ProcessConfigData

============================================================================*/
static int ProcessConfigFile( LoadState *pState, char *filename )
{
    int result = EINVAL;
    FILE *fp;
    char *pConfigData;
    char *saveFileName;
    int saveLineNumber;
    char *pFileName = NULL;

    if ( filename != NULL )
    {
        pFileName = strdup( filename );
    }

    if ( ( pState != NULL ) &&
         ( pFileName != NULL ) )
    {
        printf("ProcessConfigFile: %s\n", pFileName );

        /* save the file name and the line number within that file */
        saveFileName = pState->pFileName;
        saveLineNumber = pState->lineno;

        /* initialize the line number */
        pState->lineno = 1;
        pState->pFileName = pFileName;

        pConfigData = GetConfigData( pFileName );
        if( pConfigData != NULL )
        {
            result = ProcessConfigData( pState, pConfigData );
            free( pConfigData );
        }
        else if ( pState->required == false )
        {
            /* included file doesn't exist - that's ok */
            result = EOK;
        }

        /* restore the file name and the line number within that file */
        pState->pFileName = saveFileName;
        pState->lineno = saveLineNumber;

        if ( result != EOK )
        {
            fprintf(stderr, "Failed to process %s\n", pFileName );
        }

        free( pFileName );
    }

    return result;
}

/*==========================================================================*/
/*  ProcessConfigData                                                       */
/*!
    Process a buffer of configuration data

    The ProcessConfigData function processes a buffer of configuration
    data which has typically been loaded from a configuration file.
    The configuration data consists of lines of directives and
    variable assignments.  Directives start with an @ symbol, and
    variable assignments consist of name and value strings separated
    by white space.


    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pConfigData
            pointer to the buffer of configuration data to load

    @retval EINVAL invalid arguments
    @retval EOK file processed ok
    @retval other last error as returned by ProcessConfigLine

============================================================================*/
static int ProcessConfigData( LoadState *pState, char *pConfigData )
{
    int result = EINVAL;
    int i = 0;
    int lineidx = 0;
    char *pConfigLine;
    int rc;
    bool done = false;

    if ( ( pState != NULL ) &&
         ( pConfigData != NULL ) )
    {
        /* assume the result is ok until it is not */
        result = EOK;

        /* process configuration data one line at a time
         * until we hit a NUL character */
        while( !done )
        {
            if( pConfigData[i] == '\0' )
            {
                done = true;
            }

            if( ( pConfigData[i] == '\n' ) ||
                ( pConfigData[i] == '\0' ) )
            {
                /* replace the line break with a NUL terminator*/
                pConfigData[i] = 0;

                /* clear the working buffer and reposition
                 * the write point to the start of the buffer */
                lseek( pState->fd, 0, SEEK_SET );
                memset( pState->workbuf, 0, pState->workbufSize );

                /* perform expansion of variables within the config line */
                /* i.e any variables in the form ${varname} will be replaced
                 * with their values */
                rc = TEMPLATE_StrToFile( pState->hVarServer,
                                         &pConfigData[lineidx],
                                         pState->fd);
                if ( rc == EOK )
                {
                    /* process a configuration line */
                    rc = ProcessConfigLine( pState, pState->workbuf );
                    if ( rc != EOK )
                    {
                        LogError( pState, "Config error" );
                        result = rc;
                    }
                }
                else
                {
                    LogError( pState, "Variable Expansion error" );
                    result = rc;
                }

                /* update the line index */
                lineidx = i + 1;

                /* increment the line number */
                pState->lineno++;
            }

            i++;
        }
    }

    return result;
}

/*==========================================================================*/
/*  ProcessConfigLine                                                       */
/*!
    Process a line of configuration data

    The ProcessConfigLine function processes a line of configuration
    data which has typically been loaded from a configuration file.
    The configuration data consists of either a directives or a
    variable assignment.  Directives start with an @ symbol, and
    a variable assignment consists of a name and value string separated
    by white space.


    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pConfigLine
            pointer to a NUL terminated line of configuration data to load

    @retval EINVAL invalid arguments
    @retval EOK file processed ok
    @retval other last error as returned by ProcessConfigLine

============================================================================*/
static int ProcessConfigLine( LoadState *pState, char *pConfigLine )
{
    int result = EINVAL;

    if ( ( pState != NULL ) &&
         ( pConfigLine != NULL ) )
    {
        switch( *pConfigLine )
        {
            case '\0':
            case '#':
                /* ignore comments and blank lines */
                result = EOK;
                break;

            case '@':
                result = ProcessDirective( pState, pConfigLine );
                break;

            default:
                result = ProcessVariableAssignment( pState, pConfigLine );
                break;
        }
    }

    return result;
}

/*==========================================================================*/
/*  ProcessDirective                                                        */
/*!
    Process a configuration directive

    The ProcessDirective function processes a configuration directive
    which could be one of:

    @config
    @include
    @require
    @includedir

    @config gives info about a configuration and outputs all data following
    the directive to the output log

    @include specifies the name of an (optional) configuration file to
    include

    @require specifies the name of a configuration file to include.  If
    the file does not exist, an error is thrown.

    @includedir specifies the name of a directory to scan.  All config
    files contained in the directory will be loaded.

    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pConfigDirective
            pointer to a NUL terminated configuration directive

    @retval EINVAL invalid arguments
    @retval EOK the directive was processed ok
    @retval other error as returned by the directive processing functions

============================================================================*/
static int ProcessDirective( LoadState *pState, char *pConfigDirective )
{
    int result = EINVAL;
    char *pArg = NULL;
    char *pDirective;

    if ( ( pState != NULL ) &&
         ( pConfigDirective != NULL ) )
    {
        /* get the directive and its argument */
        pDirective = strtok_r( pConfigDirective, " ", &pArg );

        /* act on the directive */
        if ( strcmp( pConfigDirective, "@config") == 0 )
        {
            result = ProcessConfigDirective( pState, pArg );
        }
        else if ( strcmp( pConfigDirective, "@include" ) == 0 )
        {
            result = ProcessIncludeDirective( pState, pArg );
        }
        else if ( strcmp( pConfigDirective, "@require" ) == 0 )
        {
            result = ProcessRequireDirective( pState, pArg );
        }
        else if ( strcmp( pConfigDirective, "@includedir" ) == 0 )
        {
            result = ProcessIncludeDirDirective( pState, pArg );
        }
        else
        {
            LogError( pState, "unknown directive" );
            result = ENOTSUP;
        }
    }

    return result;
}

/*==========================================================================*/
/*  ProcessConfigDirective                                                  */
/*!
    Process a configuration directive

    The ProcessConfigDirective treats everything following the @config
    directive as a notice, and outputs it to the standard output.
    It is used to track the loading process of the system configuration.

    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pInfo
            pointer to the configuration information to output

    @retval EINVAL invalid arguments
    @retval EOK the directive was processed ok

============================================================================*/
static int ProcessConfigDirective( LoadState *pState, char *pInfo )
{
    int result = EINVAL;

    if ( ( pState != NULL ) &&
         ( pInfo != NULL ) )
    {
        result = EOK;

        if ( pState->verbose == true )
        {
            fprintf( stdout, "Processing %s\n", pInfo );
        }
    }

    return result;
}

/*==========================================================================*/
/*  ProcessIncludeDirective                                                 */
/*!
    Process an @include configuration directive

    The ProcessIncludeDirective treats everything following the @include
    directive as an include file path, and tries to load the include
    file as a configuration file.

    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pFilename
            pointer to the configuration file name

    @retval EINVAL invalid arguments
    @retval EOK the directive was processed ok

============================================================================*/
static int ProcessIncludeDirective( LoadState *pState, char *pFilename )
{
    int result = EINVAL;

    if ( ( pState != NULL ) &&
         ( pFilename != NULL ) )
    {
        /* included files are not mandatory */
        pState->required = false;

        if( pState->verbose == true )
        {
            fprintf( stdout, "Including %s\n", pFilename );
        }

        /* recursively process a new configuration file */
        result = ProcessConfigFile( pState, pFilename );

    }

    return result;
}

/*==========================================================================*/
/*  ProcessRequireDirective                                                 */
/*!
    Process a @require configuration directive

    The ProcessRequireDirective treats everything following the @require
    directive as an include file path, and tries to load the include
    file as a configuration file.

    The difference between @include and @require is that @require'd
    configurations must exist and load successfully

    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pFilename
            pointer to the configuration file name

    @retval EINVAL invalid arguments
    @retval EOK the directive was processed ok

============================================================================*/
static int ProcessRequireDirective( LoadState *pState, char *pFilename )
{
    int result = EINVAL;

    if ( ( pState != NULL ) &&
         ( pFilename != NULL ) )
    {
        /* included files are not mandatory */
        pState->required = true;

        if( pState->verbose == true )
        {
            fprintf( stdout, "Including %s\n", pFilename );
        }

        /* recursively process a new configuration file */
        result = ProcessConfigFile( pState, pFilename );

    }

    return result;
}

/*==========================================================================*/
/*  ProcessIncludeDirDirective                                              */
/*!
    Process an @includedir configuration directive

    The ProcessIncludeDirDirective treats everything following the @require
    directive as an include directory path, and tries to load all of the
    files in that directory as config files.

    Failed configuration files are ignored and do not affect the error
    return of this function

    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pDirname
            pointer to the configuration directory name

    @retval EINVAL invalid arguments
    @retval EOK the directive was processed ok

============================================================================*/
static int ProcessIncludeDirDirective( LoadState *pState, char *pDirname )
{
    int result = EINVAL;
    DIR *configdir = NULL;
    struct dirent *entry;

    if ( ( pState != NULL ) &&
         ( pDirname != NULL ) )
    {
        result = EOK;

        if( pState->verbose == true )
        {
            fprintf( stdout, "Processing directory: %s\n", pDirname );
        }

        configdir = opendir( pDirname );
        if( configdir != NULL )
        {
            while( entry = readdir( configdir ) )
            {
                /* included directories are not mandatory */
                pState->required = false;

                /* process configuration file */
                ProcessConfigFile( pState, entry->d_name );
            }

            closedir( configdir );
        }
    }

    return result;
}

/*==========================================================================*/
/*  ProcessVariableAssignment                                               */
/*!
    Process a configuration variable assignment

    The ProcessVariableAssignment function assumes that the configuration
    line consistes of a variable name and variable value separatted by
    a space.

    It sets the variable to the specified value.

    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        pConfig
            pointer to the configuration var/value to process

    @retval EINVAL invalid arguments or invalid variable assignment
    @retval EOK the variable assignment was processed ok
    @retval other error as returned by VAR_SetNameValue

============================================================================*/
static int ProcessVariableAssignment( LoadState *pState, char *pConfig )
{
    int result = EINVAL;
    char *pVar = NULL;
    char *pVal = NULL;
    char *ch = " ";

    if ( ( pState != NULL ) &&
         ( pConfig != NULL ) )
    {
        /* check if we have an = delimeter */
        if ( strchr( pConfig, '=' ) != NULL )
        {
            ch = "=";
        }

        /* get the variable and its value */
        pVar = strtok_r( pConfig, ch, &pVal );

        if ( ( pVar != NULL ) &&
             ( pVal != NULL ) )
        {
            if( pState->verbose == true )
            {
                fprintf( stdout, "Setting %s to %s\n", pVar, pVal );
            }

            result = VAR_SetNameValue( pState->hVarServer, pVar, pVal );
            if( result != EOK )
            {
                if ( result == ENOENT )
                {
                    LogError( pState, "Variable not found" );
                }
                else
                {
                    LogError( pState, "Variable assignment failed" );
                }
            }
        }
        else
        {
            LogError( pState, "Invalid Variable Assignment" );
        }
    }

    return result;
}

/*==========================================================================*/
/*  LogError                                                                */
/*!
    Log an error

    The LogError function generates an error output

    @param[in]
        pState
            pointer to the Load state which manages the current
            loading context

    @param[in]
        error
            pointer to a NUL terminated error string to log

============================================================================*/
void LogError( LoadState *pState, char *error )
{
    char *filename = "unknown";

    if ( ( pState != NULL ) &&
         ( error != NULL ) )
    {
        if( pState->pFileName != NULL )
        {
            filename = pState->pFileName;
        }

        fprintf( stderr,
                 "%s in %s on line %d\n",
                 error,
                 pState->pFileName,
                 pState->lineno );
    }
}

/*==========================================================================*/
/*  GetConfigData                                                           */
/*!
    Load a buffer of configuration data from a file

    The GetConfigData reads the specified file and loads its
    content into a buffer which has been allocated on the heap.
    It is the callers responsibility to deallocate the memory
    used for the configuration data.

    @param[in]
        filename
            pointer to a NUL terminated filename string specifying the
            name of the configuration file to load.

    @retval pointer to a NUL terminated configuration data buffer
    @retval NULL if the configuration data could not be loaded

============================================================================*/
static char *GetConfigData( char *filename )
{
    FILE *fp;
    char *pConfig = NULL;
    size_t taglen = strlen( CONFIG_TAG );
    size_t filesize;

    if( filename != NULL )
    {
        /* check the size of the file */
        filesize = GetFileSize( filename );
        if( filesize >= taglen )
        {
            /* open the file */
            fp = fopen( filename, "r" );
            if( fp != NULL )
            {
                /* check if it is a config file */
                if( IsConfigFile( fp ) == true )
                {
                    /* read the configuration data */
                    pConfig = ReadConfigData( fp, filesize );
                }

                /* close the file */
                fclose( fp );
            }
        }
    }

    return pConfig;
}

/*==========================================================================*/
/*  GetFileSize                                                             */
/*!
    Get the size of the specified configuration file

    The GetFileSize function determines the size of the specified file.

    @param[in]
        filename
            pointer to a NUL terminated filename string specifying the
            name of the configuration file to determine the size for.

    @retval size of the file
    @retval 0 if the file does not exist

============================================================================*/
static size_t GetFileSize( char *filename )
{
    size_t filesize = 0;
    struct stat st;

    if( lstat( filename, &st ) == 0 )
    {
        filesize = st.st_size;
    }

    return filesize;
}

/*==========================================================================*/
/*  IsConfigFile                                                            */
/*!
    Determine if the specified file is a configuration file

    The IsConfigFile function determines if the specified file is a
    configuration file.  All configuration files must start with
    an @config directive on their first line.  Any file which does not
    start this way will not be processed as a configuration file.

    After checking for the @config directive, the read point is
    reset to the beginning of the file for normal configuration
    processing,

    @param[in]
        fp
            pointer to an open FILE

    @retval true the file is a configuration file
    @retval false the file is not a configuration file

============================================================================*/
static bool IsConfigFile( FILE *fp )
{
    char buf[12];
    bool result = false;
    const char *configTag = CONFIG_TAG;
    size_t taglen = strlen( configTag );

    if( fp != NULL )
    {
        /* read taglen bytes to check if the tag is present */
        fread( buf, taglen, 1, fp );

        /* check tag */
        if( strncmp( buf, configTag, taglen ) == 0 )
        {
            /* reset pointer to the start of the file */
            fseek( fp, 0, SEEK_SET );

            /* confirm this is a config file */
            result = true;
        }
    }

    return result;
}

/*==========================================================================*/
/*  ReadConfigData                                                          */
/*!
    Read the specified file into a buffer

    The ReadConfigFile function allocates enough space for the file
    content on the HEAP and reads the file content into the buffer
    allowing enough space for a NUL terminator at the end.

    @param[in]
        fp
            pointer to an open FILE

    @param[in]
        n
            number of bytes to read from the file

    @retval pointer to the buffer of read data
    @retval NULL if the buffer could not be read

============================================================================*/
static char *ReadConfigData( FILE *fp, size_t n )
{
    char *pConfigData = NULL;

    if( fp != NULL )
    {
        pConfigData = calloc(1, n + 1 );
        if( pConfigData != NULL )
        {
            /* slurp in the file */
            fread( pConfigData, n, 1, fp );
        }
    }

    return pConfigData;
}

/*! @}
 * end of loadconfig group */

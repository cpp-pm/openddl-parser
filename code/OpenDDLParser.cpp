/*-----------------------------------------------------------------------------------------------
The MIT License (MIT)

Copyright (c) 2014 Kim Kulling

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
-----------------------------------------------------------------------------------------------*/
#include <openddlparser/OpenDDLParser.h>

#include <cassert>
#include <iostream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#  include <windows.h>
#endif // _WIN32

#define DEBUG_HEADER_NAME 

BEGIN_ODDLPARSER_NS

static const char *Version = "0.1.0";

static const char* PrimitiveTypeToken[ Value::ddl_types_max ] = {
    "bool",
    "int8",
    "int16",
    "int32",
    "int64",
    "unsigned_int8",
    "unsigned_int16",
    "unsigned_int32",
    "unsigned_int64",
    "half",
    "float",
    "double",
    "string",
    "ref"
};

static const char *BoolTrue  = "true";
static const char *BoolFalse = "false";
static const char *RefToken  = "ref";

static void logInvalidTokenError( char *in, char *exp, OpenDDLParser::logCallback callback ) {
    std::stringstream stream;
    stream << "Invalid token " << *in << ", " << exp << " expected." << std::endl;
    callback( ddl_error_msg, stream.str() );
}

static bool isIntegerType( Value::ValueType integerType ) {
    if( integerType != Value::ddl_int8 && integerType != Value::ddl_int16 && integerType != Value::ddl_int32 && integerType != Value::ddl_int64 ) {
        return false;
    }

    return true;
}

static DDLNode *createDDLNode( Identifier *id, OpenDDLParser *parser ) {
    if( nullptr == id || ddl_nullptr == parser ) {
        return ddl_nullptr;
    }

    const std::string type( id->m_buffer );
    DDLNode *parent( parser->top() );
    DDLNode *node = DDLNode::create( type, "", parent );

    return node;
}

static void logMessage( LogSeverity severity, const std::string &msg ) {
    std::string log;
    if( ddl_debug_msg == severity ) {
        log += "Debug:";
    } else if( ddl_info_msg == severity ) {
        log += "Info :";
    } else if( ddl_warn_msg == severity ) {
        log += "Warn :";
    } else if( ddl_error_msg == severity ) {
        log += "Error:";
    } else {
        log += "None :";
    }

    log += msg;
    std::cout << log;
}

OpenDDLParser::OpenDDLParser()
: m_logCallback( logMessage )
, m_ownsBuffer( false )
, m_buffer( ddl_nullptr )
, m_len( 0 )
, m_stack()
, m_context( ddl_nullptr ) {
    // empty
}

OpenDDLParser::OpenDDLParser( char *buffer, size_t len, bool ownsIt )
: m_logCallback( &logMessage )
, m_ownsBuffer( false )
, m_buffer( ddl_nullptr )
, m_len( 0 ) 
, m_context( ddl_nullptr ) {
    if( 0 != m_len ) {
        setBuffer( buffer, len, ownsIt );
    }
}

OpenDDLParser::~OpenDDLParser() {
    clear();
}

void OpenDDLParser::setLogCallback( logCallback callback ) {
    if( nullptr != callback ) {
        // install user-specific log callback
        m_logCallback = callback;
    } else {
        // install default log callback
        m_logCallback = &logMessage;
    }
}

OpenDDLParser::logCallback OpenDDLParser::getLogCallback() const {
    return m_logCallback;
}

void OpenDDLParser::setBuffer( char *buffer, size_t len, bool ownsIt ) {
    if( m_buffer && m_ownsBuffer ) {
        delete[] m_buffer;
        m_buffer = ddl_nullptr;
        m_len = 0;
    }

    m_ownsBuffer = ownsIt;
    if( m_ownsBuffer ) {
        // when we are owning the buffer we will do a deep copy
        m_buffer = new char[ len ];
        m_len = len;
        ::memcpy( m_buffer, buffer, len );
    } else {
        // when we are not owning the buffer, we just do a shallow copy
        m_buffer = buffer;
        m_len = len;
    }
}

char *OpenDDLParser::getBuffer() const {
    return m_buffer;
}

size_t OpenDDLParser::getBufferSize() const {
    return m_len;
}

void OpenDDLParser::clear() {
    if( m_ownsBuffer ) {
        delete [] m_buffer;
    }
    m_buffer = ddl_nullptr;
    m_len = 0;

    if( m_context ) {
        m_context->m_root = ddl_nullptr;
    }

    DDLNode::releaseNodes();
}

bool OpenDDLParser::parse() {
    if( 0 == m_len ) {
        return false;
    }
    
    normalizeBuffer( m_buffer, m_len );

    m_context = new Context;
    m_context->m_root = DDLNode::create( "root", "", ddl_nullptr );
    pushNode( m_context->m_root );

    // do the main parsing
    char *current( &m_buffer[ 0 ] );
    char *end( &m_buffer[ m_len - 1 ] + 1 );
    while( current != end ) {
        current = parseNextNode( current, end );
    }
    return true;
}

char *OpenDDLParser::parseNextNode( char *in, char *end ) {
    in = parseHeader( in, end );
    in = parseStructure( in, end );

    return in;
}

char *OpenDDLParser::parseHeader( char *in, char *end ) {
    if( nullptr == in || in == end ) {
        return in;
    }

    Identifier *id( ddl_nullptr );
    in = OpenDDLParser::parseIdentifier( in, end, &id );

#ifdef DEBUG_HEADER_NAME    
    if( id ) {
        std::cout << id->m_buffer << std::endl;
    }
#endif // DEBUG_HEADER_NAME

    in = getNextToken( in, end );
    Property *first( ddl_nullptr );
    if( ddl_nullptr != id ) {
        if( *in == '(' ) {
            in++;
            Property *prop( ddl_nullptr ), *prev( ddl_nullptr );
            while( *in != ')' && in != end ) {
                in = parseProperty( in, end, &prop );
                in = getNextToken( in, end );

                if( *in != ',' && *in != ')' ) {
                    logInvalidTokenError( in, ")", m_logCallback );
                    return in;
                }
                if( ddl_nullptr != prop && *in != ',' ) {
                    if( ddl_nullptr == first ) {
                        first = prop;
                    }
                    if( ddl_nullptr != prev ) {
                        prev->m_next = prop;
                    }
                    prev = prop;
                }
            }
            in++;
        }

        // set the properties
        if( ddl_nullptr != first ) {
            std::cout << id->m_buffer << std::endl;
            if( 0 == strncmp( "Metric", id->m_buffer, id->m_len ) ) {
                m_context->setProperties( first );
            } else {
                DDLNode *current( top() );
                if( current ) {
                    current->setProperties( first );
                }
            }
        }

        // store the node
        DDLNode *node( createDDLNode( id, this ) );
        if( nullptr != node ) {
            pushNode( node );
        } else {
            std::cerr << "nullptr returned by creating DDLNode." << std::endl;
        }

        Name *name( ddl_nullptr );
        in = OpenDDLParser::parseName( in, end, &name );
        if( ddl_nullptr != name ) {
            const std::string nodeName( name->m_id->m_buffer );
            node->setName( nodeName );
        }
    }

    return in;
}

char *OpenDDLParser::parseStructure( char *in, char *end ) {
    if( nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    if( *in == '{' ) {
        in++;
        in = getNextToken( in, end );
        Value::ValueType type( Value::ddl_none );
        size_t arrayLen( 0 );
        in = OpenDDLParser::parsePrimitiveDataType( in, end, type, arrayLen );
        if( Value::ddl_none != type ) {
            in = getNextToken( in, end );
            if( *in == '{' ) {
                DataArrayList *dtArrayList( ddl_nullptr );
                Value *values( ddl_nullptr );
                if( 1 == arrayLen ) {
                    in = parseDataList( in, end, &values );
                    if( ddl_nullptr != values ){
                        DDLNode *currentNode( top() );
                        if( ddl_nullptr != currentNode ) {
                            currentNode->setValue( values );
                        }
                    }
                } else if( arrayLen > 1 ) {
                    in = parseDataArrayList( in, end, &dtArrayList );
                    if( ddl_nullptr != dtArrayList ) {
                        DDLNode *currentNode( top() );
                        if( ddl_nullptr != currentNode ) {
                            currentNode->setDataArrayList( dtArrayList );
                        }
                    }
                } else {
                    std::cerr << "0 for array is invalid." << std::endl;
                }
            }

            in = getNextToken( in, end );
            if( *in != '}' ) {
                logInvalidTokenError( in, "}", m_logCallback );
            }
        } else {
            in = parseHeader( in, end );
            in = parseStructure( in, end );
        }
    } else {
        in++;
        logInvalidTokenError( in, "{", m_logCallback );
        return in;

    }

    in++;

    return in;
}

void OpenDDLParser::pushNode( DDLNode *node ) {
    if( nullptr == node ) {
        return;
    }

    m_stack.push_back( node );
}

DDLNode *OpenDDLParser::popNode() {
    if( m_stack.empty() ) {
        return ddl_nullptr;
    }

    DDLNode *topNode( top() );
    m_stack.pop_back();

    return topNode;
}

DDLNode *OpenDDLParser::top() {
    if( m_stack.empty() ) {
        return ddl_nullptr;
    }
    
    DDLNode *top( m_stack.back() );
    return top;
}

DDLNode *OpenDDLParser::getRoot() const {
    if( nullptr == m_context ) {
        return ddl_nullptr;
    }

    return m_context->m_root;
}

Context *OpenDDLParser::getContext() const {
    return m_context;
}

void OpenDDLParser::normalizeBuffer( char *buffer, size_t len ) {
    if( nullptr == buffer || 0 == len ) {
        return;
    }

    size_t writeIdx( 0 );
    char *end( &buffer[ len ] + 1 );
    for( size_t readIdx = 0; readIdx<len; ++readIdx ) {
        char *c( &buffer[readIdx] );
        // check for a comment
        if( !isComment<char>( c, end ) ) {
            buffer[ writeIdx ] = buffer[ readIdx ];
            writeIdx++;
        } else {
            readIdx++;
            // skip the comment and the rest of the line
            while( !isEndofLine( buffer[ readIdx ] ) ) {
                readIdx++;
            }
            buffer[writeIdx] = '\n';
            writeIdx++;
        }
    }

    if( writeIdx < len ) {
        buffer[ writeIdx ] = '\0';
    }
}

char *OpenDDLParser::parseName( char *in, char *end, Name **name ) {
    *name = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    // ignore blanks
    in = getNextToken( in, end );
    if( *in != '$' && *in != '%' ) {
        return in;
    }

    NameType ntype( GlobalName );
    if( *in == '%' ) {
        ntype = LocalName;
    }

    Name *currentName( ddl_nullptr );
    Identifier *id( ddl_nullptr );
    in = parseIdentifier( in, end, &id );
    if( id ) {
        currentName = new Name( ntype, id );
        if( currentName ) {
            *name = currentName;
        }
    }
    
    return in;
}

char *OpenDDLParser::parseIdentifier( char *in, char *end, Identifier **id ) {
    *id = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    // ignore blanks
    in = getNextToken( in, end );
    
    // staring with a number is forbidden
    if( isNumeric<const char>( *in ) ) {
        return in;
    }

    // get size of id
    size_t idLen( 0 );
    char *start( in );
    while( !isSeparator( *in ) && ( in != end ) && *in != '(' && *in != ')' ) {
        in++;
        idLen++;
    }
    
    const size_t len( idLen + 1 );
    Identifier *newId = new Identifier( len, new char[ len ] );
    ::strncpy( newId->m_buffer, start, newId->m_len-1 );
    newId->m_buffer[ newId->m_len - 1 ] = '\0';
    *id = newId;

    return in;
}

char *OpenDDLParser::parsePrimitiveDataType( char *in, char *end, Value::ValueType &type, size_t &len ) {
    type = Value::ddl_none;
    len = 0;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    for( unsigned int i = 0; i < Value::ddl_types_max; i++ ) {
        const size_t prim_len( strlen( PrimitiveTypeToken[ i ] ) );
        if( 0 == strncmp( in, PrimitiveTypeToken[ i ], prim_len ) ) {
            type = ( Value::ValueType ) i;
            break;
        }
    }

    if( Value::ddl_none == type ) {
        in = getNextToken( in, end );
        return in;
    } else {
        in += strlen( PrimitiveTypeToken[ type ] );
    }

    bool ok( true );
    if( *in == '[' ) {
        ok = false;
        in++;
        char *start( in );
        while ( in != end ) {
            in++;
            if( *in == ']' ) {
                len = atoi( start );
                ok = true;
                in++;
                break;
            }
        }
    } else {
        len = 1;
    }
    if( !ok ) {
        type = Value::ddl_none;
    }

    return in;
}

char *OpenDDLParser::parseReference( char *in, char *end, std::vector<Name*> &names ) {
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    if( 0 != strncmp( in, RefToken, strlen( RefToken ) ) ) {
        return in;
    } else {
        const size_t refTokenLen( strlen( RefToken ) );
        in += refTokenLen;
    }

    in = getNextToken( in, end );
    if( '{' != *in ) {
        return in;
    } else {
        in++;
    }

    in = getNextToken( in, end );
    Name *nextName( ddl_nullptr );
    in = parseName( in, end, &nextName );
    if( nextName ) {
        names.push_back( nextName );
    }
    while( '}' != *in ) {
        in = getNextSeparator( in, end );
        if( ',' == *in ) {
            in = parseName( in, end, &nextName );
            if( nextName ) {
                names.push_back( nextName );
            }
        } else {
            break;
        }
    }

    return in;
}

char *OpenDDLParser::parseBooleanLiteral( char *in, char *end, Value **boolean ) {
    *boolean = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    char *start( in );
    size_t len( 0 );
    while( !isSeparator( *in ) && in != end ) {
        in++;
        len++;
    }
    len++;
    int res = ::strncmp( BoolTrue, start, strlen( BoolTrue ) );
    if( 0 != res ) {
        res = ::strncmp( BoolFalse, start, strlen( BoolFalse ) );
        if( 0 != res ) {
            *boolean = ddl_nullptr;
            return in;
        }
        *boolean = ValueAllocator::allocPrimData( Value::ddl_bool );
        (*boolean)->setBool( false );
    } else {
        *boolean = ValueAllocator::allocPrimData( Value::ddl_bool );
        (*boolean)->setBool( true );
    }

    return in;
}

char *OpenDDLParser::parseIntegerLiteral( char *in, char *end, Value **integer, Value::ValueType integerType ) {
    *integer = ddl_nullptr;
    if( nullptr == in || in == end ) {
        return in;
    }

    if( !isIntegerType( integerType ) ) {
        return in;
    }

    in = getNextToken( in, end );
    char *start( in );
    while( !isSeparator( *in ) && in != end ) {
        in++;
    }

    if( isNumeric( *start ) ) {
        const int value( atoi( start ) );
        *integer = ValueAllocator::allocPrimData( integerType );
        switch( integerType ) {
            case Value::ddl_int8:
                    ( *integer )->setInt8( (int8) value );
                    break;
            case Value::ddl_int16:
                    ( *integer )->setInt16( ( int16 ) value );
                    break;
            case Value::ddl_int32:
                    ( *integer )->setInt32( ( int32 ) value );
                    break;
            case Value::ddl_int64:
                    ( *integer )->setInt64( ( int64 ) value );
                    break;
            default:
                break;
        }
    } 

    return in;
}

char *OpenDDLParser::parseFloatingLiteral( char *in, char *end, Value **floating ) {
    *floating = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    char *start( in );
    while( !isSeparator( *in ) && in != end ) {
        in++;
    }

    // parse the float value
    bool ok( false );
    if( isNumeric( *start ) ) {
        ok = true;
    } else {
        if( *start == '-' ) {
            if( isNumeric( *(start+1) ) ) {
                ok = true;
            }
        }
    }

    if( ok ) {
        const float value( ( float ) atof( start ) );
        *floating = ValueAllocator::allocPrimData( Value::ddl_float );
        ( *floating )->setFloat( value );
    }

    return in;
}

char *OpenDDLParser::parseStringLiteral( char *in, char *end, Value **stringData ) {
    *stringData = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    size_t len( 0 );
    char *start( in );
    if( *start == '\"' ) {
        start++;
        in++;
        while( *in != '\"' && in != end ) {
            in++;
            len++;
        }

        *stringData = ValueAllocator::allocPrimData( Value::ddl_string, len + 1 );
        ::strncpy( ( char* ) ( *stringData )->m_data, start, len );
        ( *stringData )->m_data[len] = '\0';
        in++;
    }

    return in;
}

static void createPropertyWithData( Identifier *id, Value *primData, Property **prop ) {
    if( nullptr != primData ) {
        ( *prop ) = new Property( id );
        ( *prop )->m_primData = primData;
    }
}

char *OpenDDLParser::parseHexaLiteral( char *in, char *end, Value **data ) {
    *data = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    if( *in != '0' ) {
        return in;
    }

    in++;
    if( *in != 'x' && *in != 'X' ) {
        return in;
    }

    in++;
    bool ok( true );
    char *start( in );
    int pos( 0 );
    while( !isSeparator( *in ) && in != end ) {
        if( ( *in < '0' && *in > '9' ) || ( *in < 'a' && *in > 'f' ) || ( *in < 'A' && *in > 'F' ) ) {
            ok = false;
            break;
        }
        pos++;
        in++;
    }

    if( !ok ) {
        return in;
    }

    int value( 0 );
    while( pos > 0 ) {
        pos--;
        value += hex2Decimal( *start ) * static_cast<int>( pow( 16.0, pos ) );
        start++;
    }

    *data = ValueAllocator::allocPrimData( Value::ddl_int32 );
    (*data)->setInt32( value );

    return in;
}

char *OpenDDLParser::parseProperty( char *in, char *end, Property **prop ) {
    *prop = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    Identifier *id( ddl_nullptr );
    in = parseIdentifier( in, end, &id );
    if( nullptr != id ) {
        in = getNextToken( in, end );
        if( *in == '=' ) {
            in++;
            in = getNextToken( in, end );
            Value *primData( ddl_nullptr );
            if( isInteger( in, end ) ) {
                in = parseIntegerLiteral( in, end, &primData );
                createPropertyWithData( id, primData, prop );
            } else if( isFloat( in, end ) ) {
                in = parseFloatingLiteral( in, end, &primData );
                createPropertyWithData( id, primData, prop );
            } else if( isStringLiteral( *in ) ) { // string data
                in = parseStringLiteral( in, end, &primData );
                createPropertyWithData( id, primData, prop );
            } else {                          // reference data
                std::vector<Name*> names;
                in = parseReference( in, end, names );
                if( !names.empty() ) {
                    Reference *ref = new Reference( names.size(), &names[ 0 ] );
                    ( *prop ) = new Property( id );
                    ( *prop )->m_ref = ref;
                }
            }
        } 
    }

    return in;
}

char *OpenDDLParser::parseDataList( char *in, char *end, Value **data ) {
    *data = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    if( *in == '{' ) {
        in++;
        Value *current( ddl_nullptr ), *prev( ddl_nullptr );
        while( '}' != *in ) {
            current = ddl_nullptr;
            in = getNextToken( in, end );
            if( isInteger( in, end ) ) {
                in = parseIntegerLiteral( in, end, &current );
            } else if( isFloat( in, end ) ) {
                in = parseFloatingLiteral( in, end, &current );
            } else if( isStringLiteral( *in ) ) {
                in = parseStringLiteral( in, end, &current );
            } else if( isHexLiteral( in, end ) ) {
                in = parseHexaLiteral( in, end, &current );
            }

            if( ddl_nullptr != current ) {
                if( ddl_nullptr == *data ) {
                    *data = current;
                    prev = current;
                } else {
                    prev->setNext( current );
                    prev = current;
                }
            }

            in = getNextSeparator( in, end );
            if( ',' != *in && '}' != *in && !isSpace( *in ) ) {
                break;
            }
        }
        in++;
    }

    return in;
}

char *OpenDDLParser::parseDataArrayList( char *in, char *end, DataArrayList **dataList ) {
    *dataList = ddl_nullptr;
    if( ddl_nullptr == in || in == end ) {
        return in;
    }

    in = getNextToken( in, end );
    if( *in == '{' ) {
        in++;
        Value *current( ddl_nullptr );
        DataArrayList *prev( ddl_nullptr ), *currentDataList( ddl_nullptr );
        do {
            in = parseDataList( in, end, &current );
            if( ddl_nullptr != current ) {
                if( ddl_nullptr == prev ) {
                    *dataList = new DataArrayList;
                    (*dataList)->m_dataList = current;
                    prev = *dataList;
                } else {
                    currentDataList = new DataArrayList;
                    if( ddl_nullptr != prev ) {
                        prev->m_next = currentDataList;
                        prev = currentDataList;
                    }
                }
            }
        } while( ',' == *in && in != end );
    }

    return in;
}

const char *OpenDDLParser::getVersion() {
    return Version;
}

END_ODDLPARSER_NS

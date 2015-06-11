/*
 *  keafile.cpp
 *  LibKEA
 *
 *  Created by Sam Gillingham on 26/07/2013.
 *  Copyright 2013 LibKEA. All rights reserved.
 *
 *  This file is part of LibKEA.
 *
 *  Permission is hereby granted, free of charge, to any person 
 *  obtaining a copy of this software and associated documentation 
 *  files (the "Software"), to deal in the Software without restriction, 
 *  including without limitation the rights to use, copy, modify, 
 *  merge, publish, distribute, sublicense, and/or sell copies of the 
 *  Software, and to permit persons to whom the Software is furnished 
 *  to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be 
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR 
 *  ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF 
 *  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "kea.h"
#include "keaproj.h"

#include <sys/types.h>
#include <sys/stat.h>

bool IsSupportedDataType(kealib::KEAImageIO *pImageIO, uint32_t nBand)
{
    // Imagine does not support the 64 bit int type
    kealib::KEADataType type = pImageIO->getImageBandDataType(nBand);
    if( ( type == kealib::kea_64int ) || ( type == kealib::kea_64uint ) )
    {
        return false;
    }
    return true;
}

time_t getModifiedTime(char *fileName)
{
    time_t modTime = 0;
    // Uses stat call but syntax is slightly different 
    // Unix vs Windows
#ifdef WIN32
    struct _stat buf;
    if( _stat(fileName, &buf) == 0)
    {
        modTime = buf.st_mtime;
    }
#else
    struct stat buf;
    if( stat(filename, &buf) == 0 )
    {
        modTime = buf.st_mtime;
    }
#endif    
    return modTime;
}

void *
keaFileTitleIdentifyAndOpen(char *fileName, long *fileType, char *inFileMode)
{
#ifdef KEADEBUG
    if( inFileMode == NULL )
    {
        keaDebugOut( "%s %s %ld NULL\n", __FUNCTION__, fileName, *fileType, inFileMode);
    }
    else
    {
        keaDebugOut( "%s %s %ld %s\n", __FUNCTION__, fileName, *fileType, inFileMode);
    }
#endif
    Eerr_ErrorReport* err = NULL;
    // check if writing
    if( ( inFileMode != NULL ) && ( EFIO_MODE_CREATE( inFileMode ) == EMSC_TRUE ) )
    {
        KEA_File *pKEAFile = new KEA_File();
        pKEAFile->pH5File = NULL;
        pKEAFile->pImageIO = NULL; // will set this on LayerCreate
        pKEAFile->sFilePath = fileName;
        pKEAFile->pProj = NULL;
        pKEAFile->modTime = 0;
        pKEAFile->bUpdate = true;
        return pKEAFile;
    }

    KEA_File  *pKEAFile = NULL;
    bool isKEA = false;
    try
    {
        // is this a KEA file?
        isKEA = kealib::KEAImageIO::isKEAImage( fileName );
    }
    catch (kealib::KEAIOException &e)
    {
        keaDebugOut( "Exception in %s: %s\n", __FUNCTION__, e.what());
        isKEA = false;
    }

    if( isKEA )
    {
        *fileType = 0;
        // I *think* they only want us to open when inFileMode != NULL....
        if( inFileMode != NULL ) 
        {
            H5::H5File *pH5File = NULL;
            kealib::KEAImageIO *pImageIO = NULL;
            try
            {
                if( ( inFileMode == NULL ) || EFIO_MODE_READONLY(inFileMode) )
                {
                    pH5File = kealib::KEAImageIO::openKeaH5RDOnly( fileName );
                }
                else
                {
                    pH5File = kealib::KEAImageIO::openKeaH5RW( fileName );
                }
                pImageIO = new kealib::KEAImageIO();
                pImageIO->openKEAImageHeader( pH5File );

                pKEAFile = new KEA_File();
                pKEAFile->pH5File = pH5File;
                pKEAFile->pImageIO = pImageIO;
                pKEAFile->sFilePath = fileName;
                kealib::KEAImageSpatialInfo *pSpatialInfo = pImageIO->getSpatialInfo();
                // turn from WKT into something Imagine understands
                pKEAFile->pProj = WKTToMapProj(pSpatialInfo->wktString.c_str(), pKEAFile->sProjName, pKEAFile->sUnits);
                pKEAFile->modTime = getModifiedTime(fileName); // for keaFileDataModTimeGet
                uint32_t nBands = pImageIO->getNumOfImageBands();
                for( uint32_t n = 0; n < nBands; n++ )
                {
                    uint32_t nBand = n + 1;
                    if( !IsSupportedDataType( pImageIO, nBand ) )
                    {
#ifdef KEADEBUG                        
                        keaDebugOut( "Band %d has type unsupported by Imagine\n", n );
#endif                        
                        continue;
                    }
                    
                    // the 'real' layer
                    KEA_Layer *pLayer = new KEA_Layer();
                    pLayer->pKEAFile = pKEAFile;
                    std::string sName = pImageIO->getImageBandDescription(nBand);
                    // Imagine doesn't like spaces
                    std::replace(sName.begin(), sName.end(), ' ', '_');
#ifdef KEADEBUG                        
                    keaDebugOut( "added layer '%s'\n", sName.c_str());
#endif                        
                    pLayer->sName = sName;
                    pLayer->nBand = nBand;
                    pLayer->bIsOverview = false;
                    pLayer->bIsMask = false;
                    pLayer->bMaskIsReal = false;
                    pLayer->nOverview = 99999;
                    pLayer->eKEAType = pImageIO->getImageBandDataType(nBand);
                    pLayer->nXSize = pSpatialInfo->xSize;
                    pLayer->nYSize = pSpatialInfo->ySize;
                    pLayer->nBlockSize = pImageIO->getImageBlockSize(nBand);
                    pKEAFile->aLayers.push_back(pLayer);
                    
                    // mask - Imagine 2015 requires us to have one for each band
                    KEA_Layer *pMask = new KEA_Layer();
                    pMask->pKEAFile = pKEAFile;
                    char *name = estr_Sprintf( NULL, (char*)"%s:Mask", &err, 
                                sName.c_str(), NULL );
                    HANDLE_ERR(err, NULL);
                    pMask->sName = name;
                    emsc_Free(name);
                    pMask->nBand = nBand;
                    pMask->bIsOverview = false;
                    pMask->bIsMask = true;
                    pMask->bMaskIsReal = pImageIO->maskCreated(nBand);
                    pMask->nOverview = 99999;
                    pMask->eKEAType = kealib::kea_8uint;
                    pMask->nXSize = pSpatialInfo->xSize;
                    pMask->nYSize = pSpatialInfo->ySize;
                    pMask->nBlockSize = pImageIO->getImageBlockSize(nBand);
                    pKEAFile->aLayers.push_back(pMask);
                    
                    // do the overviews
                    unsigned int nOverViews = pImageIO->getNumOfOverviews(nBand);
                    for(unsigned int o = 0; o < nOverViews; o++ )
                    {
                        unsigned int nOverview = o + 1;
                        KEA_Layer *pOverview = new KEA_Layer();
                        pOverview->pKEAFile = pKEAFile;
                        char *name = estr_Sprintf( NULL, (char*)"%s:Overview_%d", &err, 
                                sName.c_str(), nOverview, NULL );
                        HANDLE_ERR(err, NULL);
                        pOverview->sName = name;
                        emsc_Free(name);
                        pOverview->nBand = nBand;
                        pOverview->bIsOverview = true;
                        pOverview->bIsMask = false;
                        pOverview->bMaskIsReal = false;
                        pOverview->nOverview = nOverview;
                        pOverview->eKEAType = pLayer->eKEAType;
                        uint64_t xsize, ysize;
                        pImageIO->getOverviewSize(nBand, nOverview, &xsize, &ysize);
                        pOverview->nXSize = xsize;
                        pOverview->nYSize = ysize;
                        pOverview->nBlockSize = pImageIO->getOverviewBlockSize(nBand, nOverview);
                        pKEAFile->aLayers.push_back(pOverview);
                        
                        // mask for the overview
                        KEA_Layer *pOverviewMask = new KEA_Layer();
                        pOverviewMask->pKEAFile = pKEAFile;
                        name = estr_Sprintf( NULL, (char*)"%s:Overview_%d:Mask", &err, 
                                sName.c_str(), nOverview, NULL );
                        HANDLE_ERR(err, NULL);
                        pOverviewMask->sName = name;
                        emsc_Free(name);
                        pOverviewMask->nBand = nBand;
                        pOverviewMask->bIsOverview = true;
                        pOverviewMask->bIsMask = true;
                        pOverviewMask->bMaskIsReal = false;
                        pOverviewMask->nOverview = nOverview;
                        pOverviewMask->eKEAType = kealib::kea_8uint;
                        pImageIO->getOverviewSize(nBand, nOverview, &xsize, &ysize);
                        pOverviewMask->nXSize = xsize;
                        pOverviewMask->nYSize = ysize;
                        pOverviewMask->nBlockSize = pImageIO->getOverviewBlockSize(nBand, nOverview);
                        pKEAFile->aLayers.push_back(pOverviewMask);
                    }
                }
            }
            catch (kealib::KEAIOException &e)
            {
#ifdef KEADEBUG
                keaDebugOut( "Error during opening %s: %s\n", fileName, e.what());
#endif
                // was a problem - can't be a valid file
                delete pImageIO;
                delete pKEAFile;
                pKEAFile = NULL;
                pImageIO = NULL;
            }
        }
    }
#ifdef KEADEBUG
    keaDebugOut( "open returning %p\n", pKEAFile);
#endif

	return pKEAFile;
}

long
keaFileClose(void *fileHandle)
{
    KEA_File *pKEAFile = (KEA_File*)fileHandle;
#ifdef KEADEBUG
    keaDebugOut( "%s %s %p\n", __FUNCTION__, pKEAFile->sFilePath.c_str(), pKEAFile );
#endif

    try
    {
        if( pKEAFile->pImageIO != NULL )
        {
            // closes the pKEAFile->pH5File also
            pKEAFile->pImageIO->close();
            pKEAFile->pH5File = NULL;
        }
    }
    catch (kealib::KEAIOException &e)
    {
        keaDebugOut( "Exception in %s: %s\n", __FUNCTION__, e.what());
    }
    delete pKEAFile->pImageIO;
    pKEAFile->pImageIO = NULL;
    for( std::vector<KEA_Layer*>::iterator itr = pKEAFile->aLayers.begin();
            itr != pKEAFile->aLayers.end(); itr++ )
    {
        KEA_Layer *pLayer = (*itr);
        delete pLayer;
    }

    if( pKEAFile->pProj != NULL )
    {
        eprj_ProjectionFree(&pKEAFile->pProj);
    }
    delete pKEAFile;
    pKEAFile = NULL;
	return 0;
}

long
keaFileLayerNamesGet(void *fileHandle, unsigned long *count, char ***layerNames)
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p\n", __FUNCTION__, fileHandle );
#endif
    KEA_File *pKEAFile = (KEA_File*)fileHandle;

    long rCode = -1;
    unsigned long layerCount = 0;

    // work out how many we have
    for( std::vector<KEA_Layer*>::iterator itr = pKEAFile->aLayers.begin();
            itr != pKEAFile->aLayers.end(); itr++ )
    {
        KEA_Layer *pCandidate = (*itr);
        if( !pCandidate->bIsOverview && !pCandidate->bIsMask )
        {
            layerCount++;
        }
    }

    if( layerCount > 0 )
    {
        *count = layerCount;
        *layerNames = emsc_New(layerCount, char *);
        layerCount = 0;

        for( std::vector<KEA_Layer*>::iterator itr = pKEAFile->aLayers.begin();
            itr != pKEAFile->aLayers.end(); itr++ )
        {
            KEA_Layer *pCandidate = (*itr);
            if( !pCandidate->bIsOverview && !pCandidate->bIsMask )
            {
                (*layerNames)[layerCount] = estr_Duplicate((char*)pCandidate->sName.c_str());
                layerCount++;
            }
        }
    }
    else
    {
        *count = 0;
        *layerNames = NULL;
    }
    rCode = 0;

	return rCode;
}

long
keaFileDataRead(void *fileHandle, char *dataName, unsigned char **MIFDataObject,
        unsigned long *MIFDataSize, char **MIFDataDictionary, char **MIFDataType)
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p %s\n", __FUNCTION__, fileHandle, dataName );
#endif
    Eerr_ErrorReport *err = NULL;
    long rCode = 0; // always need to succeed or Imagine will not display
    *MIFDataObject = NULL;
    *MIFDataDictionary = NULL;
    *MIFDataType = NULL;
    *MIFDataSize = 0;

    // currently we only support getting the histogram bin function in this manner
    // will be in the form:
    // :LayerName:OverviewName:Descriptor_Table:#Bin_Function#
    char *pszNameCopy = estr_Duplicate(dataName);
    char *pszLastColon = strrchr(pszNameCopy, ':');
    // is it looking for the bin function?
    if( ( pszLastColon != NULL ) &&  ( strcmp(pszLastColon+1, "#Bin_Function#") == 0 ) )
    {
#ifdef KEADEBUG        
        keaDebugOut( "Found #Bin_Function# %s\n", pszNameCopy );
#endif        
        *pszLastColon = '\0'; // put a null there and look at the next one back
        pszLastColon = strrchr(pszNameCopy, ':');
        if( ( pszLastColon != NULL ) && (strcmp(pszLastColon+1, "Descriptor_Table" ) == 0 ) )
        {
            //fprintf( stderr, "Found Descriptor_Table\n" );
            // now find the second colon
            char *pszSecondColon = strchr(&pszNameCopy[1], ':');
            if( pszSecondColon != NULL )
            {
                *pszSecondColon = '\0';
                char *pszLayerName = &pszNameCopy[1];
#ifdef KEADEBUG                
                keaDebugOut( "looking for layer %s\n", pszLayerName );
#endif
                unsigned long dtype, width, height, bWidth, bHeight, compression;
                KEA_Layer *pKEALayer;
                if( keaLayerOpen(fileHandle, pszLayerName, &dtype, &width, &height, 
                            &compression, &bWidth, &bHeight, (void**)&pKEALayer) == 0 )
                {
                    //fprintf( stderr, "Found layer\n" );
                    Edsc_BinFunction *pBinFn = keaLayerGetHistoBinFunction(pKEALayer);
                    if( pBinFn != NULL )
                    {
#ifdef KEADEBUG
                        keaDebugOut( "Found bin function\n" );
#endif                        
                        // make 'MIFable'
                        EMIF_CADDR *pMIFableObject;
                        Emif_Design *pDesign;
                        edsc_BinFunctionConvertToMIFable(pBinFn, (void**)&pMIFableObject, &pDesign, &err);
                        HANDLE_ERR(err, -1)
    
                        // this code doesn't work post 8.4. I had an alternative (see "Geometric Models Example" on developer.lggi.com)
                        // but this is much tidier
                        *MIFDataSize = emif_MIFableObjectConvertToMIF(pMIFableObject, pDesign, MIFDataObject,
                            MIFDataDictionary, MIFDataType, &err);
                        HANDLE_ERR(err, -1)

                        emif_ObjectFree(&pMIFableObject, pDesign, &err);
                        HANDLE_ERR(err, -1)
                        emif_DesignDestroy(pDesign);
                        emsc_Free(pBinFn);
                        rCode = 0;
#ifdef KEADEBUG
                        keaDebugOut( "bin function success\n" );
#endif                        
                    }
                    // does nothing, but for completeness
                    keaLayerClose(pKEALayer);
                }
            }
        }
    }
    emsc_Free(pszNameCopy);
#ifdef KEADEBUG
    if( *MIFDataSize == 0 )
    {
        keaDebugOut( "bin function NOT found\n");
    }
#endif
    return rCode;
}

long
keaFileDataWrite( void  *fileHandle, char  *dataName,  unsigned char  *MIFDataObject, 
    unsigned long  MIFDataSize, char  *MIFDataDictionary, char  *MIFDataType )
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p %s\n", __FUNCTION__, fileHandle, dataName );
#endif
    Eerr_ErrorReport *err = NULL;
    long rCode = -1;
    // currently we only support getting the histogram bin function in this manner
    // will be in the form:
    // :LayerName:OverviewName:Descriptor_Table:#Bin_Function#
    char *pszNameCopy = estr_Duplicate(dataName);
    char *pszLastColon = strrchr(pszNameCopy, ':');
    // is it looking for the bin function?
    if( ( pszLastColon != NULL ) &&  ( strcmp(pszLastColon+1, "#Bin_Function#") == 0 ) )
    {
#ifdef KEADEBUG        
        fprintf( stderr, "Found #Bin_Function# %s\n", pszNameCopy );
#endif        
        *pszLastColon = '\0'; // put a null there and look at the next one back
        pszLastColon = strrchr(pszNameCopy, ':');
        if( ( pszLastColon != NULL ) && (strcmp(pszLastColon+1, "Descriptor_Table" ) == 0 ) )
        {
            //fprintf( stderr, "Found Descriptor_Table\n" );
            // now find the second colon
            char *pszSecondColon = strchr(&pszNameCopy[1], ':');
            if( pszSecondColon != NULL )
            {
                *pszSecondColon = '\0';
                char *pszLayerName = &pszNameCopy[1];
#ifdef KEADEBUG                
                fprintf( stderr, "looking for layer %s\n", pszLayerName );
#endif
                unsigned long dtype, width, height, bWidth, bHeight, compression;
                KEA_Layer *pKEALayer;
                if( keaLayerOpen(fileHandle, pszLayerName, &dtype, &width, &height, 
                            &compression, &bWidth, &bHeight, (void**)&pKEALayer) == 0 )
                {
                    //fprintf( stderr, "Found layer\n" );
                    // convert from MIF to bin function structure
                    EMIF_CADDR *pBinObject;
                    Emif_Design *pDesign;
                    emif_MIFableObjectConvertFromMIF((void**)&pBinObject, &pDesign, MIFDataObject,
                            MIFDataDictionary, MIFDataType, &err);
                    HANDLE_ERR(err, -1)
                    
                    Edsc_BinFunction *pBinFn;
                    edsc_BinFunctionConvertFromMIFable(&pBinFn, pBinObject, pDesign, &err);
                    HANDLE_ERR(err, -1)
                    
                    // set the data
                    keaLayerSetHistoBinFunction(pKEALayer, pBinFn);

                    emif_ObjectFree(&pBinObject, pDesign, &err);
                    HANDLE_ERR(err, -1)
                    emif_DesignDestroy(pDesign);
                    emsc_Free(pBinFn);
                    
                    // does nothing, but for completeness
                    keaLayerClose(pKEALayer);
                    rCode = 0;
#ifdef KEADEBUG
                    keaDebugOut( "bin function set success\n" );
#endif                        
                }
            }
        }
    }
    emsc_Free(pszNameCopy);        
    return rCode;
}

long
keaFileDataDestroy(void *fileHandle, char *dataName )
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p %s\n", __FUNCTION__, fileHandle, dataName );
#endif
    
    return 0;
}

long
keaFileFlush( void *fileHandle )    
{
#ifdef KEADEBUG
    keaDebugOut( "%s\n", __FUNCTION__ );
#endif
    KEA_File *pKEAFile = (KEA_File*)fileHandle;
    if( pKEAFile->pH5File != NULL )
    {
        pKEAFile->pH5File->flush(H5F_SCOPE_LOCAL);
    }
    
    return 0;
}
    
long
keaFileDataModTimeGet(void *fileHandle, char *dataName, time_t *lastModTime)
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p %s\n", __FUNCTION__, fileHandle, dataName );
#endif
    /* In theory we could extract the mod time of each HDF5 object */
    /* But for now we just return the modified time of the file */
    KEA_File *pKEAFile = (KEA_File*)fileHandle;
    *lastModTime = pKEAFile->modTime;

    return 0;
}

long
keaFileLayerNamesSet( void  *fileHandle,  unsigned long  count, 
 char  **oldLayerNames, char  **newLayerNames )
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p %ld\n", __FUNCTION__, fileHandle, count );
#endif
    KEA_File *pKEAFile = (KEA_File*)fileHandle;
    
    for( unsigned long oldN = 0; oldN < count; oldN++ )
    {
        std::string sOldName = oldLayerNames[oldN];
        // need to do overviews, masks etc with same base name
        std::string sOldNameBase = sOldName + ':';
        for( std::vector<KEA_Layer*>::iterator itr = pKEAFile->aLayers.begin();
            itr != pKEAFile->aLayers.end(); itr++ )
        {
            KEA_Layer *pLayer = (*itr);
            std::string sNewName = newLayerNames[oldN];
            if( !pLayer->bIsOverview && !pLayer->bIsMask && (pLayer->sName == sOldName ))
            {
                unsigned int nBand = pLayer->nBand;
#ifdef KEADEBUG
                keaDebugOut( "renaming %s -> %s\n", sOldName.c_str(), sNewName.c_str() );
#endif
                try
                {
                    pKEAFile->pImageIO->setImageBandDescription(nBand, sNewName);
                    pLayer->sName = sNewName;
                }
                catch (kealib::KEAIOException &e)
                {
#ifdef KEADEBUG
                    keaDebugOut( "Error during renaming: %s\n", e.what());
#endif           
                }   
            }
            else if( (pLayer->bIsOverview || pLayer->bIsMask) && 
                (pLayer->sName.compare(0, sOldNameBase.size(), sOldNameBase) == 0 ))
            {
                // is an overview or mask attached to the layer to be renamed
                std::string sNewNameBase = sNewName + ':';
#ifdef KEADEBUG
                std::string sDebugOldName = pLayer->sName;
#endif
                pLayer->sName.replace(0, sOldNameBase.size(), sNewNameBase);
#ifdef KEADEBUG
                keaDebugOut( "renaming mask/overview %s -> %s\n", sDebugOldName.c_str(), pLayer->sName.c_str() );
#endif
            }
        }
    }
    return 0;
}


long 
keaFileRasterDataOrderGet(void  *fileHandle, unsigned long  *order)
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p\n", __FUNCTION__, fileHandle );
#endif
    // always BSQ - see keaInstanceRasterDataOrderTypesGet
    *order = 0;
    return 0;
}

long
keaFileRasterDataOrderSet( void  *fileHandle,  unsigned long  order, 
 unsigned long  count)
{
#ifdef KEADEBUG
    keaDebugOut( "%s %p\n", __FUNCTION__, fileHandle );
#endif
    // we don't support changing this so just pretend we succeeded
    return 0;
}
 
// Don't need to implement the following. Yes I know keaFileModeGet
// might be a way to get around the file size limit in the open dialog
// in early versions of Imagine - but it was only added to Imagine later

// Imagine uses System calls if these don't exist
/*
extern "C" long keaFileModeGet(char  *fileName, mode_t  *mode);

long
keaFileModeGet(char  *fileName, mode_t  *mode)
{
#ifdef KEADEBUG
    keaDebugOut( "%s %s\n", __FUNCTION__, fileName );
#endif

    return -1;
}

long
keaFileModTimeGet(char *fileName, time_t *modTime)
{
#ifdef KEADEBUG
    keaDebugOut( "%s %s\n", __FUNCTION__, fileName );
#endif

    return -1;
}
*/

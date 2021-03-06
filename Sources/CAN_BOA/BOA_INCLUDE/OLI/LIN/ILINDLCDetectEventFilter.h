///////////////////////////////////////////////////////////
//  ILINDLCDetectEventFilter.h
//  Defines the ILINDLCDetectEventFilter interface
///////////////////////////////////////////////////////////

/// @todo update doxygen comments

#if !defined(__OLI_ILINDLCDETECTEVENTFILTER_INCLUDED__)
#define __OLI_ILINDLCDETECTEVENTFILTER_INCLUDED__

// include used interface

#include "../Common/AutoPtr.h"
#include "../Common/IEventFilter.h"
#include "../Common/ErrorInterfaces.h"
#include "../Common/ErrorProxies.h"

#include "../Common/BeginNamespace.h"

/// \addtogroup GROUP_OLI_LIN_FILTERS

/// @{

class ILINDLCDetectEventFilter;

/**
 * This function instantiates an object supporting ILINDLCDetectEventFilter. See \ref BinaryCompatibility for an explanation
 * of why it is needed.
 *
 * NOTE that clients are encouraged to access this function via the wrapper ILINDLCDetectEventFilter::Create().
 *
 * \param[in]  frameIDMask
 * \param[in]  frameIDValue
 * \param[in]  dlcMask
 * \param[in]  dlcValue
 * \param[out] ppLinDlcDetectEventFilter    A pointer to an object supporting ILINDLCDetectEventFilter. The object will
 *                                          already have a reference count of 1 and must be reference-counted by the caller,
 *                                          using the object's methods AddRef() and Release(). This is easily done by wrapping
 *                                          the object pointer in an instance of the AutoPtr class, which will be done
 *                                          automatically if the caller accesses ILINDLCDetectEventFilter_Create() via the wrapper
 *                                          ILINDLCDetectEventFilter::Create().
 *
 * \return A pointer to an interface based on IError, describing the error which occurred during this function. NULL if no error
 * occurred. See \ref ErrorReporting for more information on how errors are reported.
 */
OLL_API IError* OLI_CALL ILINDLCDetectEventFilter_Create( uint8 frameIDMask, uint8 frameIDValue, uint8 dlcMask, uint8 dlcValue, ILINDLCDetectEventFilter** ppLinDlcDetectEventFilter );

/** A specialized event filter class for LINDLCDetect events.
*/

OLI_INTERFACE ILINDLCDetectEventFilter 
    : public IEventFilter
{
protected:

    /// Lifetime of filter instances is controlled by reference counting.

    virtual ~ILINDLCDetectEventFilter() OLI_NOTHROW {};

public:

    /// \name LIN-specific interface
	/// @{

    virtual uint8 OLI_CALL GetDLCMask() const OLI_NOTHROW = 0;
	virtual uint8 OLI_CALL GetDLCValue() const OLI_NOTHROW = 0;

	/// @}

    /**
     * This is a helper method which wraps ILINDLCDetectEventFilter_Create(): see \ref BinaryCompatibility and \ref ErrorReporting
     * for an explanation of why it is needed.
     */
    static AutoPtr<ILINDLCDetectEventFilter> OLI_CALL 
    Create ( uint8 frameIDMask
           , uint8 frameIDValue
           , uint8 dlcMask
           , uint8 dlcValue )
    {
        ILINDLCDetectEventFilter* pLinDlcDetectEventFilter = NULL;
        CheckAndThrow( ILINDLCDetectEventFilter_Create( frameIDMask, frameIDValue, dlcMask, dlcValue, &pLinDlcDetectEventFilter ) );

        // The wrapped method has already AddRef-ed the pointer, so we tell AutoPtr to take ownership of the pointer without a
        // further AddRef.
        return AutoPtr<ILINDLCDetectEventFilter>( pLinDlcDetectEventFilter, false );
    }
};

/// @}

#include "../Common/EndNamespace.h"

#endif // !defined(__OLI_ILINDLCDETECTEVENTFILTER_INCLUDED__)

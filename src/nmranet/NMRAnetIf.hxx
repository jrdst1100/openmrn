/** \copyright
 * Copyright (c) 2013, Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 * 
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file NMRAnetIf.hxx
 * This file provides the generic NMRAnet interface.
 *
 * @author Stuart W. Baker
 * @date 18 September 2013
 */

#ifndef _NMRAnetIf_hxx_
#define _NMRAnetIf_hxx_

#include <cstdint>

#include "executor/Service.hxx"
#include "executor/StateFlow.hxx"
#include "nmranet/NMRAnet.hxx"
#include "utils/BufferQueue.hxx"
#include "utils/macros.h"

namespace NMRAnet
{

/** 48-bit NMRAnet Node ID type */
typedef uint64_t NodeID;

/** Alias to a 48-bit NMRAnet Node ID type */
typedef uint16_t NodeAlias;

/** Container of both a NodeID and NodeAlias */
struct NodeHandle
{
    NodeID id; /**< 48-bit NMRAnet Node ID */
    NodeAlias alias; /**< alias to NMRAnet Node ID */

    /** Compare to NodeHandle instances.
     * @param o object to compare to
     * @return boolean result of compare
     */
    bool operator==(const NodeHandle& o) const
    {
        return id == o.id && alias == o.alias;
    }
};

/** The generic interface for NMRAnet network interfaces
 */
class If : public Service
{
public:
    /** Constructor.
     */
    If(NodeID node_id)
        : Service(nmranetExecutor),
          nodeID(node_id),
          receiveFlow(this)
    {
    }

    /** Virtual default destructor.
     */
    virtual ~If()
    {
    }  

    /** Known Message type indicators.
     */
    enum MTI
    {
        MTI_NONE                      = 0x0000, /**< invalid MTI */
        MTI_INITIALIZATION_COMPLETE   = 0x0100, /**< initialization complete */
        MTI_VERIFY_NODE_ID_ADDRESSED  = 0x0488, /**< verify a Node ID */
        MTI_VERIFY_NODE_ID_GLOBAL     = 0x0490, /**< verify a Node ID globally */
        MTI_VERIFIED_NODE_ID_NUMBER   = 0x0170, /**< respond to a verify Node ID request */
        MTI_OPTIONAL_INTERACTION_REJECTED = 0x0068, /**< rejected request */
        MTI_TERMINATE_DUE_TO_ERROR    = 0x00A8, /**< terminate due to some error */
        MTI_PROTOCOL_SUPPORT_INQUIRY  = 0x0828, /**< inquire on supported protocols */
        MTI_PROTOCOL_SUPPORT_REPLY    = 0x0668, /**< reply with supported protocols */
        MTI_CONSUMER_IDENTIFY         = 0x08F4, /**< query about consumers */
        MTI_CONSUMER_IDENTIFIED_RANGE   = 0x04A4, /**< consumer broadcast about a range of consumers */
        MTI_CONSUMER_IDENTIFIED_UNKNOWN = 0x04C7, /**< consumer broadcast, validity unknown */
        MTI_CONSUMER_IDENTIFIED_VALID   = 0x04C4, /**< consumer broadcast, valid state */
        MTI_CONSUMER_IDENTIFIED_INVALID = 0x04C5, /**< consumer broadcast, invalid state */
        MTI_CONSUMER_IDENTIFIED_RESERVED = 0x04C6, /**< reserved for future use */
        MTI_PRODUCER_IDENTIFY         = 0x0914, /**< query about producers */
        MTI_PRODUCER_IDENTIFIED_RANGE   = 0x0524, /**< producer broadcast about a range of producers */
        MTI_PRODUCER_IDENTIFIED_UNKNOWN = 0x0547, /**< producer broadcast, validity unknown */
        MTI_PRODUCER_IDENTIFIED_VALID   = 0x0544, /**< producer broadcast, valid state */
        MTI_PRODUCER_IDENTIFIED_INVALID = 0x0545, /**< producer broadcast, invalid state */
        MTI_PRODUCER_IDENTIFIED_RESERVED = 0x0546, /**< reserved for future use */
        MTI_EVENTS_IDENTIFY_ADDRESSED = 0x0968, /**< request identify all of a node's events */
        MTI_EVENTS_IDENTIFY_GLOBAL    = 0x0970, /**< request identify all of every node's events */
        MTI_LEARN_EVENT               = 0x0594, /**< */
        MTI_EVENT_REPORT              = 0x05B4, /**< */
        MTI_XPRESSNET                 = 0x09C0, /**< */
        MTI_IDENT_INFO_REQUEST        = 0x0DE8, /**< request node identity */
        MTI_IDENT_INFO_REPLY          = 0x0A08, /**< node identity reply */
        MTI_DATAGRAM                  = 0x1C48, /**< datagram */
        MTI_DATAGRAM_OK               = 0x0A28, /**< datagram received okay */
        MTI_DATAGRAM_REJECTED         = 0x0A48, /**< datagram rejected by receiver */
        MTI_STREAM_INITIATE_REQUEST   = 0x0CC8, /**< Stream initiate request */
        MTI_STREAM_INITIATE_REPLY     = 0x0868, /**< Stream initiate reply */
        MTI_STREAM_DATA               = 0x1F88, /**< stream data */
        MTI_STREAM_PROCEED            = 0x0888, /**< stream flow control */
        MTI_STREAM_COMPLETE           = 0x08A8, /**< stream terminate connection */
        
        MTI_MODIFIER_MASK = 0x0003, /**< modifier within Priority/Type mask */
        MTI_EVENT_MASK    = 0x0004, /**< event number present mask */
        MTI_ADDRESS_MASK  = 0x0008, /**< Address present mask */
        MTI_SIMPLE_MASK   = 0x0010, /**< simple protocol mask */
        MTI_TYPE_MASK     = 0x03e0, /**< type within priority mask */
        MTI_PRIORITY_MASK = 0x0c00, /**< priority mask */
        MTI_DATAGRAM_MASK = 0x1000, /**< stream or datagram mask */
        MTI_SPECIAL_MASK  = 0x2000, /**< special mask */
        MTI_RESERVED_MASK = 0xc000, /**< reserved mask */

        MTI_MODIFIER_SHIFT =  0, /**< modifier within Priority/Type shift */
        MTI_EVENT_SHIFT    =  2, /**< event number present shift */
        MTI_ADDRESS_SHIFT  =  3, /**< Address present shift */
        MTI_SIMPLE_SHIFT   =  4, /**< simple protocol shift */
        MTI_TYPE_SHIFT     =  5, /**< type within priority shift */
        MTI_PRIORITY_SHIFT = 10, /**< priority shift */
        MTI_DATAGRAM_SHIFT = 12, /**< stream or datagram shift */
        MTI_SPECIAL_SHIFT  = 13, /**< special shift */
        MTI_RESERVED_SHIFT = 14  /**< reserved shift */

    };

    /** Status of the pysical layer link */
    enum LinkStatus
    {
        UP,  /**< link is up and ready for transmit */
        DOWN /**< link is down and unable to transmit */
    };
    
    /** Write a message onto the interface.
     * @param mti Message Type Indicator
     * @param src source node ID, 0 if unavailable
     * @param dst destination node ID, 0 if unavailable
     * @param data NMRAnet packet data
     * @return 0 upon success
     */
    virtual int if_write(MTI mti, NodeID src, NodeHandle dst, Buffer *data) = 0;

    /** Process receive data.
     * @param mti Message Type Indicator
     * @param src source node ID, 0 if unavailable
     * @param dst destination node ID, 0 if unavailable
     * @param data NMRAnet packet data
     */
    void rx_data(MTI mti, NodeHandle src, NodeID dst, Buffer *data);
    
    /** Can be used by the application to determine if the link is up or down.
     * @return current link status
     */
    virtual LinkStatus link_status() = 0;

    /** Message ID's that we can receive */
    enum MessageId
    {
        RECEIVE = NMRANET_IF_BASE,
        SEND
    };

    /** Message structure for an incoming NMRAnet Message
     */
    struct InMessage
    {
        Buffer *data; /** message payload */
        NodeID dst; /**< destination Node ID, 0 if unavailable */
        NodeHandle src; /**< source node ID, 0 if unavailable */
        MTI mti; /**< message type identifier */
    };
    
    /** Message structure for an incoming NMRAnet Message
     */
    struct OutMessage
    {
        NodeID src; /**< destination Node ID, 0 if unavailable */
        NodeHandle dst; /**< source node ID, 0 if unavailable */
        MTI mti; /**< message type identifier */
        uint8_t data[] __attribute__ ((aligned (__BIGGEST_ALIGNMENT__))); /**< message payload */
    };
    
protected:
    /** Get the MTI address present value field.
     * @param mti MTI to extract field value from
     * @return true if MTI is an addressed message, else false
     */
    static bool get_mti_address(MTI mti)
    {
        return (mti & MTI_ADDRESS_MASK);
    }

    /** Get the MTI datagram or stream value field.
     * @param mti MTI to extract field value from
     * @return true if MTI is a datagram or stream, else false
     */
    static bool get_mti_datagram(MTI mti)
    {
        return (mti & MTI_DATAGRAM_MASK);
    }

    /** Get the MTI priority (value 0 through 3).
     * @param mti MTI to extract field value from
     * @return priority value 0 through 3
     */
    static unsigned int mti_priority(MTI mti)
    {
        return (mti & MTI_PRIORITY_MASK) >> MTI_PRIORITY_SHIFT;
    }

    /** 48-bit NMRAnet node id associated with this interface */
    NodeID nodeID;

    /** Translate an incoming Message ID into a StateFlow instance.
     * @param id itentifier to translate
     * @return StateFlow corresponding the given ID, NULL if not found
     */
    virtual StateFlowBase *lookup(uint32_t id)
    {
        switch (id)
        {
            default:
                break;
            case RECEIVE:
                return &receiveFlow;
         }
        return NULL;
    }

    /** Maximum size of a static addressed message payload */
    static const size_t MAX_ADDRESSED_SIZE;

private:
    /** Handles receiving of incoming messages */
    STATE_FLOW_START(ReceiveFlow, 4)
    STATE_FLOW_END()
    
    /** State machine instance that handles receiving incoming messages */
    ReceiveFlow receiveFlow;

    DISALLOW_COPY_AND_ASSIGN(If);
};

/** Operator overload for post increment */
inline If::MTI operator ++ (If::MTI &value, int)
{
    If::MTI result = value;
    value = static_cast<If::MTI>(static_cast<int>(value) + 1);
    return result;
}

/** Operator overload for pre increment */
inline If::MTI& operator ++ (If::MTI &value)
{
    value = static_cast<If::MTI>(static_cast<int>(value) + 1);
    return value;
}

/** Operator overload for post decrement */
inline If::MTI operator -- (If::MTI &value, int)
{
    If::MTI result = value;
    value = static_cast<If::MTI>(static_cast<int>(value) - 1);
    return result;
}

/** Operator overload for pre decrement */
inline If::MTI& operator -- (If::MTI &value)
{
    value = static_cast<If::MTI>(static_cast<int>(value) - 1); 
    return value;
}

}; /* namespace NMRAnet */

#endif /* _NMRAnetIf_hxx_ */

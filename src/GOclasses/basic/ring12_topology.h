/*****************************************************************************
 *   Copyright (C) 2008, 2009 Advanced Concepts Team (European Space Agency) *
 *   act@esa.int                                                             *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program; if not, write to the                           *
 *   Free Software Foundation, Inc.,                                         *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.               *
 *****************************************************************************/

// 13/01/2009: Initial version by Francesco Biscani.

#ifndef PAGMO_RING12_TOPOLOGY_H
#define PAGMO_RING12_TOPOLOGY_H

#include "../../config.h"
#include "graph_topology.h"

///Bi-directional +1+2 ring topology
/** In such a ring, every node is connected with a direct neigbour and his direct neighbour. */
class __PAGMO_VISIBLE ring12_topology: public graph_topology {
	public:
		/// Constructor.
		ring12_topology();
		/// Copy constructor.
		ring12_topology(const ring12_topology &);
		
		/// \see base_topology::clone
		virtual ring12_topology *clone() const {return new ring12_topology(*this);}
		
		/// \see base_topology::push_back
		virtual void push_back(const size_t& id);
		
		/// \see base_topology::id_object()
		virtual std::string id_object() const { return id_name(); }
		
	private:	
		/// Tracks the id of the first tracked node.
		size_t	a;
		/// Tracks the id of the second tracked node.
		size_t	b;
		/// Tracks the id of the third tracked node.
		size_t	c;
		/// Tracks the id of the fourth tracked node.
		size_t	d;
		
		/// \see graph_topology::operator=
		ring12_topology &operator=(const ring12_topology &);
};

#endif
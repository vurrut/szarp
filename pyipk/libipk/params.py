#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# vim: set fileencoding=utf-8 :


try :
	import urlparse as up
except ImportError :
	import urllib.parse as up

try :
	from StringIO import StringIO
except ImportError :
	from io import StringIO

import lxml

from libipk import errors

from lxml import etree

from .filesource import *

toUtf8 = lambda s: s.decode('utf8')

class PNode :
	def __init__( self , doc , parent , node ) :
		self.doc = doc
		self.parent = parent
		self.node = node
		self.__build_children()

	def __len__( self ) :
		return len(self.children)

	def __getitem__( self, key ) :
		return self.children[key]

	def touch( self ) :
		self.doc.touch()

	def getparent( self ) :
		return self.parent
	
	def index( self , child ) :
		return self.node.index(child.node)

	def getline( self ) :
		return self.node.sourceline

	def toline( self ) :
#        ns = n.nsmap[None]
#        n.nsmap[None] = ''
#        out = toUtf8( etree.tostring( n , pretty_print = True , encoding = 'utf8' , method = 'xml' ) ).partition('\n')[0]
#        n.nsmap[None] = ns
#        return out
		if isinstance(self.node.tag,str) :
			tag = self.node.tag.replace( '{%s}' % self.node.nsmap[self.node.prefix] , '%s:' % self.node.prefix if self.node.prefix != None else  '' )
			out = '<%s '% tag + ' '.join(['%s="%s"'%(a,self.node.get(a)) for a in self.node.attrib]) + '>'
		else :
			out = toUtf8( etree.tostring( self.node , pretty_print = True , encoding = 'utf8' , method = 'xml' ) ).partition('\n')[0]
		return out

	def tostring( self ) :
		return etree.tostring( self.node , pretty_print = True , encoding='utf8' , method = 'xml' , xml_declaration=True )

	def __repr__( self ) :
		return '('+object.__repr__(self)+','+str(self.node)+')'

	def replace( self , lxmlnode ) :
		if self.parent != None :
			self.parent.node.replace( self.node , lxmlnode )
		self.node = lxmlnode
		self.__rebuild_children()

	def remove( self , child ) :
		try :
			self.node.remove( child.node )
			self._remove( child )
		except ValueError :
			raise ValueError('That child is not mine: ' + self.toline())

	def _remove( self , child ) :
		self.children.remove( child )
		child.parent = None
		self.touch()

	def insert( self , pos , child ) :
		if child in self.children :
			return
		if pos < 0 : pos = len(self.children)
		self.node.insert( pos , child.node )
		self._insert( pos , child )

	def _insert( self , pos , child ) :
		self.children.insert( pos , child )
		child.parent = self
		self.touch()

	def create_child( self , node ) :
		n = self.__create( node )
		self.insert( -1 , n )
		return n

	def __create( self , node ) :
		return self.__class__( self.doc , self , node )

	def rebuild( self ) :
		self.__rebuild_children()

	def __build_children( self ) :
		if self.node == None :
			raise ValueError('Node must be defined')

		self.children = []

		for n in self.node :
			self.children.append( self.__create(n) )

	def _index( self , node , start = 0 ) :
		for i in range(start,len(self.children)) :
			if self.children[i].node == node :
				return i
		return -1

	def __rebuild_children( self ) :
		for i in range(len(self.node)) :
			if i < len(self) and self.children[i].node == self.node[i] :
				continue

			idx = self._index( self.node[i] , i )
			if idx >= 0 :
				# swap children
				ci   = self.children[i  ]
				cidx = self.children[idx]

				self._remove( ci   )
				self._remove( cidx )

				self._insert( i , cidx )
				self._insert( idx , ci )
			else :
				# insert new child
				self._insert( i , self.__create(self.node[i]) )

			self.touch()

		if len(self.node) < len(self.children) :
			# remove additional children
			torm = list(self.children[len(self.node):])
			for c in torm : self._remove( c )
			self.touch()

		for c in self.children : c.rebuild()

	def find_path( self ) :
		path = []
		node = self

		parent = node.getparent()
		while parent != None :
			path.insert( 0 , parent.index( node ) )
			node = parent
			parent = node.getparent()

		return path

	def get_by_path( self , path ) :
		node = self
		for p in path :
			try :
				node = node[p]
			except IndexError :
				return None
		return node

class Params :
	def __init__( self , relaxng = None , namespaces = None , treeclass = PNode ) :
		self.rng_schema = relaxng
		self.rng_log = None
		self.changed = False
		self.doc = None
		self.treeclass = treeclass
		self.nsmap = {}

	def getroot( self ) :
		return self.root

	def touch( self ) :
		self.changed = True

	def get_filesource( self ) :
		return self.fs

	def reload( self , force = None ) :
		if not force and self.changed :
			return False

		self.doc = etree.parse( StringIO( self.fs.read() ) )
		self.rebuild_tree()
		self.changed = False

		return True

	def open( self , fs ) :
		if self.doc != None :
			raise IOError('File already opened')
		self.fs = fs
		self.doc = etree.parse( StringIO( fs.read() ) )
		self.root = self.treeclass( self , None , self.doc.getroot() )
		self.changed = False
		self.validate_raise()

	def save( self , fs = None ) :
		fs = self.fs if fs == None else fs
		self.changed = False
		try :
			self.validate_raise()
		finally :
			fs.write( etree.tostring( self.doc , pretty_print=True , method = 'xml' , xml_declaration=True , encoding=self.doc.docinfo.encoding ) )

	def close( self ) :
		if self.changed :
			return False
		self.close_force()
		return True

	def close_force( self ) :
		self.doc = None
		self.root = None
		self.changed = False

	def validate( self , schema=None ) :
		if schema == None :
			schema = self.rng_schema
		if schema == None :
			return True

		relaxng_doc = etree.parse(schema)
		relaxng = etree.RelaxNG(relaxng_doc)
		if relaxng.validate( self.root.node ) :
			self.rng_log = None
			return True
		else :
			self.rng_log = repr( relaxng.error_log )
			return False

	def validate_raise( self , schema=None ) :
		if not self.validate( schema ) :
			raise ValueError("XML doesn't validate")

	def validate_log( self ) :
		return self.rng_log

	def rebuild_tree( self , nodes = [] ) :
		''' Rebuilds PNode tree to consits with lxml tree.
		
		nodes -- suggests where are canges, but not provide whole information
		returns -- list of PNodes equivalent to lxml nodes 
		'''
		if self.root.node != self.doc.getroot() :
			self.root.replace( self.doc.getroot() )
		else :
			self.root.rebuild()

		return [ self.get_pnode( self.find_node_path( n ) ) for n in nodes ]

	def find_node_path( self , node ) :
		path = []

		parent = node.getparent()
		while parent != None :
			path.insert( 0 , parent.index( node ) )

			node = parent
			parent = node.getparent()

		if node != self.getroot().node :
			raise ValueError('Node not in my tree')

		return path

	def get_pnode( self , path ) :
		return self.getroot().get_by_path( path )

def params_file( filename , *l , **m ) :
	p = Params( *l , **m )
	p.open( FS_local(filename) )
	return p

def params_fs( fs , *l , **m ) :
	p = Params( *l , **m )
	p.open( fs )
	return p

import re

class ParamsSearch :
	def __init__( self , roots , pattern ) :
		self.stack  = list(reversed(roots))
		self.pattern= re.compile(pattern)

	def next( self , breaker = None ) :
		while len(self.stack) > 0 and (breaker == None or breaker()) :
			node   = self.stack.pop()
			self.stack += reversed( node.children )
			if self.pattern.search( node.toline() ) :
				return node.find_path()
		return None


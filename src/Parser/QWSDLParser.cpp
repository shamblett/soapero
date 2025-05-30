/*
 * QWSDLParser.cpp
 *
 *  Created on: 12 juil. 2017
 *      Author: lgruber
 */

#include <QFile>
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>

#include "Model/ComplexType.h"
#include "Model/SimpleType.h"
#include "Model/Type.h"

#include "WSDLAttributes.h"

#include "QWSDLParser.h"

QWSDLParser::QWSDLParser()
{
	m_bWaitForSoapEnvelopeFault = false;

	m_pWSDLData = QSharedPointer<QWSDLData>(new QWSDLData());

	m_pService = Service::create();
	m_pListTypes = TypeList::create();
	m_pListRequestResponseElements = RequestResponseElementList::create();
	m_pListMessages = MessageList::create();
	m_pListOperations = OperationList::create();
	m_pListAttributes = AttributeList::create();
	m_pListElements = ElementList::create();

	initXMLAttributes();

	m_iLogIndent = 0;
}

QWSDLParser::~QWSDLParser()
{

}

void QWSDLParser::initXMLAttributes()		// https://www.w3.org/2001/xml.xsd
{
	// https://www.w3.org/2001/xml.xsd#att_lang
	AttributeSharedPtr pAttribute = Attribute::create();
	pAttribute->setNamespace("xml");
	pAttribute->setName("lang");
	SimpleTypeSharedPtr pSimpleType = SimpleType::create();
	pSimpleType->setVariableTypeFromString("xs", "xs:string");
	pSimpleType->setNamespace("xml");
	pSimpleType->setName("lang");
	m_pListTypes->append(pSimpleType);
	pAttribute->setType(pSimpleType);
	m_pListAttributes->append(pAttribute);
}

void QWSDLParser::setLogIndent(int iIdent)
{
	m_iLogIndent = iIdent;
}

void QWSDLParser::setWSDLData(const QSharedPointer<QWSDLData>& pWSDLData)
{
	m_pWSDLData = pWSDLData;
}

void QWSDLParser::setInitialNamespaceUri(const QString& szNamespaceUri)
{
	pushCurrentTargetNamespace(szNamespaceUri);
}

bool QWSDLParser::parse(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QXmlStreamReader::TokenType iTokenType;

	while (!xmlReader.atEnd() && bRes)
	{
		iTokenType = xmlReader.readNext();

		if (iTokenType == QXmlStreamReader::StartElement)
		{
			QString szTagName =  xmlReader.name().toString();

			logParser("processing: " + szTagName);
			incrLogIndent();

			// WSDL files
			if (szTagName == TAG_DEFINITIONS) {
				bRes = readDefinitions(xmlReader);
			}
			// XSD files
			if(szTagName == TAG_SCHEMA){
				bRes = readSchema(xmlReader);
			}

			decrLogIndent();
		}

		if(iTokenType == QXmlStreamReader::EndDocument)
		{
			endDocument();
		}
	}

	if (xmlReader.hasError()) {
		qCritical("[XML] Event list parse error: %s", qPrintable(xmlReader.errorString()));
		return false;
	}

	if (!bRes) {
		qCritical("[XML] Invalid event item");
		return false;
	}

	return true;
}

bool QWSDLParser::endDocument()
{
	TypeSharedPtr pType;
	TypeList::const_iterator type;
	TypeListSharedPtr pListTypesToRemove = TypeList::create();;

	RequestResponseElementList::iterator iter_rre;

	ElementList::const_iterator iter_element;
	ElementSharedPtr pElement;
	ElementSharedPtr pTmpElement;
	ElementListSharedPtr pElementList;

	AttributeList::iterator attr;
	AttributeSharedPtr pAttribute;

	OperationList::iterator operation;

	// TODO: resolve ref for attributes

	// Resolve ref for element
	for(iter_element = m_pListElements->constBegin(); iter_element != m_pListElements->constEnd(); ++iter_element)
	{
		pTmpElement = (*iter_element);
		if(pTmpElement->needRef()){
			ElementSharedPtr pRefElement = getElementByRef(pTmpElement->getRefValue());
			if(pRefElement){
				pTmpElement->setRef(pRefElement);
			}
		}
	}

	// Resolve types
	for(type = m_pListTypes->constBegin(); type != m_pListTypes->constEnd(); ++type)
	{
		if((*type)->getTypeMode() == Type::TypeComplex)
		{
			ComplexTypeSharedPtr pComplexType = qSharedPointerCast<ComplexType>(*type);

			if(pComplexType->getExtensionType()) {
				if(pComplexType->getExtensionType()->getTypeMode() == Type::TypeUnknown) {
					pType = getTypeByName(pComplexType->getExtensionType()->getLocalName(), pComplexType->getExtensionType()->getNamespace());
					pComplexType->setExtensionType(pType);
				}
			}

			pElementList = pComplexType->getElementList();
			if(pElementList->count() > 0) {
				//Parcours elment list

				for(iter_element = pElementList->constBegin(); iter_element != pElementList->constEnd(); ++iter_element)
				{
					pTmpElement = (*iter_element);
					if(pTmpElement->hasRef()){
						pElement = pTmpElement->getRef();
					}else{
						pElement = pTmpElement;
					}

					// Resolve element type
					if(pElement->getType() && pElement->getType()->getTypeMode() == Type::TypeUnknown) {
						pType = getTypeByName(pElement->getType()->getLocalName(), pElement->getType()->getNamespace(), pListTypesToRemove);
						while(pType && pType->getTypeMode() == Type::TypeUnknown) {
							pListTypesToRemove->append(pType);
							pType = getTypeByName(pElement->getType()->getLocalName(), pElement->getType()->getNamespace(), pListTypesToRemove);
						}
						pElement->setType(pType);
					}
				}

			}
			if(pComplexType->getAttributeList()->count() > 0){
				//Parcours attr list

				for(attr = pComplexType->getAttributeList()->begin();
				attr != pComplexType->getAttributeList()->end(); ++attr){
					if((*attr)->hasRef()){
						pAttribute = (*attr)->getRef();
					}else{
						pAttribute = *attr;
					}

					if(pAttribute->getType() && pAttribute->getType()->getTypeMode() == Type::TypeUnknown){
						pType = getTypeByName(pAttribute->getType()->getLocalName(), pAttribute->getType()->getNamespace());
						pAttribute->setType(pType);
					}
				}
			}
		}
	}

	for(type = pListTypesToRemove->constBegin(); type != pListTypesToRemove->constEnd(); ++type){
		m_pListTypes->removeAll(*type);
	}
	pListTypesToRemove->clear();

	for(iter_rre = m_pListRequestResponseElements->begin(); iter_rre != m_pListRequestResponseElements->end(); ++iter_rre)
	{
		RequestResponseElementSharedPtr pRequestResponseElement = (*iter_rre);

		TypeSharedPtr pType = pRequestResponseElement->getType();
		ComplexTypeSharedPtr pComplexType;
		if(pType && pType->getTypeMode() == Type::TypeComplex){
			pComplexType = qSharedPointerCast<ComplexType>(pType);
		}
		if(pType->getTypeMode() == Type::TypeUnknown){
			if(pComplexType.isNull()){
				TypeSharedPtr pTypeTmp = getTypeByName(pType->getLocalName(), pType->getNamespace());
				if(pTypeTmp->getTypeMode() == Type::TypeComplex){
					pComplexType = qSharedPointerCast<ComplexType>(pTypeTmp);
					pRequestResponseElement->setType(pTypeTmp);
				}
			}
		}

		if(pComplexType){
			if(pComplexType->getExtensionType()) {
				if(pComplexType->getExtensionType()->getTypeMode() == Type::TypeUnknown)
				{
					pType = getTypeByName(
							pComplexType->getExtensionType()->getLocalName(),
							pComplexType->getExtensionType()->getNamespace());
					pComplexType->setExtensionType(pType);
				}
			}

			pElementList = pComplexType->getElementList();
			if(pElementList->count() > 0)
			{
				for(iter_element = pElementList->constBegin(); iter_element != pElementList->constEnd(); ++iter_element)
				{
					pTmpElement = (*iter_element);

					if(pTmpElement->hasRef()){
						pElement = pTmpElement->getRef();
					}else{
						pElement = pTmpElement;
					}

					if(pElement->getType() && pElement->getType()->getTypeMode() == Type::TypeUnknown) {
						pType = getTypeByName(pElement->getType()->getLocalName(),
											  pElement->getType()->getNamespace());
						pElement->setType(pType);
					}
				}
			}
			if(pComplexType->getAttributeList()->count() > 0) {
				//Parcours attr list

				for(attr = pComplexType->getAttributeList()->begin();
				attr != pComplexType->getAttributeList()->end(); ++attr) {

					if( (*attr)->getType()->getTypeMode() == Type::TypeUnknown) {
						pType = getTypeByName((*attr)->getType()->getLocalName(),
											  (*attr)->getType()->getNamespace());
						(*attr)->setType(pType);
					}


				}
			}
		}
	}

	// Resolve recursive inclusion (class1 includes class2 and class2 includes class1)
	// by using pointers (only check elements type for now).
	// TODO: check attributes type too.
	for(type = m_pListTypes->constBegin(); type != m_pListTypes->constEnd(); ++type)
	{
		if((*type)->getTypeMode() == Type::TypeComplex) {
			ComplexTypeSharedPtr pComplexType = qSharedPointerCast<ComplexType>(*type);

			// Add soap envelope default fault type if exists
			if(pComplexType->isSoapEnvelopeFault()){
				for(operation = m_pListOperations->begin(); operation != m_pListOperations->end(); ++operation){
					(*operation)->setSoapEnvelopeFaultType(pComplexType);
				}
			}

			pElementList = pComplexType->getElementList();
			if(pElementList && pElementList->count() > 0){
				for(iter_element = pElementList->constBegin(); iter_element != pElementList->constEnd(); ++iter_element)
				{
					pTmpElement = (*iter_element);
					if(pTmpElement->hasRef()){
						pElement = pTmpElement->getRef();
					}else{
						pElement = pTmpElement;
					}

					if(pElement->getType() && pElement->getType()->getTypeMode() == Type::TypeComplex){
						ComplexTypeSharedPtr pComplexTypeElem = qSharedPointerCast<ComplexType>(pElement->getType());

						if(pComplexTypeElem->getElementList() && pComplexTypeElem->getElementList()->count() > 0){
							ElementList::const_iterator iter;
							for(iter = pComplexTypeElem->getElementList()->constBegin(); iter != pComplexTypeElem->getElementList()->constEnd(); ++iter){
								if((*iter)->getType() && (*iter)->getType()->getNameWithNamespace() == pComplexType->getNameWithNamespace()){
									// No need to use a pointer if it's a list, or if it's already nested
									if(((*iter)->getMaxOccurs() == 1) && !(*iter)->isNested()){
										(*iter)->setIsPointer(true);
										pElement->setIsPointer(true);
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return true;
}


bool QWSDLParser::readXMLNamespaces(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QXmlStreamNamespaceDeclarations decls = xmlReader.namespaceDeclarations();
	QXmlStreamNamespaceDeclarations::const_iterator iter;

	QString szAttrLocalName;
	QString szAttrValue;

	for(iter = decls.constBegin(); iter != decls.constEnd(); ++iter)
	{
		const QXmlStreamNamespaceDeclaration& decl = (*iter);

		szAttrLocalName = decl.prefix().toString();
		szAttrValue = decl.namespaceUri().toString();

		m_pWSDLData->addNamespaceDeclaration(szAttrLocalName, szAttrValue);
	}

	return bRes;
}

bool QWSDLParser::readDefinitions(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	// Read XML namespaces
	bRes = readXMLNamespaces(xmlReader);

	// Parse attributes
	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();
	QString szTargetNamespace;
	if(bRes){
		if(xmlAttrs.hasAttribute(ATTR_NAME))
		{
			m_pService->setName(xmlAttrs.value(ATTR_NAME).toString());
		}
		if(xmlAttrs.hasAttribute(ATTR_TARGET_NAMESPACE))
		{
			szTargetNamespace = xmlAttrs.value(ATTR_TARGET_NAMESPACE).toString();
			m_pService->setTargetNamespace(szTargetNamespace);
		}
	}

	// Enter target namespace if any
	if(!szTargetNamespace.isEmpty()){
		bRes = pushCurrentTargetNamespace(szTargetNamespace);
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		logParser("processing: " + xmlReader.name().toString());

		if (xmlReader.name().toString() == TAG_TYPES) {
			bRes = readTypes(xmlReader);
		}else if (xmlReader.name().toString() == TAG_MESSAGE) {
			bRes = readMessage(xmlReader);
		}else if (xmlReader.name().toString() == TAG_PORTTYPE) {
			bRes = readPortType(xmlReader);
		}else if (xmlReader.name().toString() == TAG_BINDING) {
			bRes = readBinding(xmlReader);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	// Exit target namespace if any
	if(!szTargetNamespace.isEmpty()){
		popCurrentTargetNamespace();
	}

	return bRes;
}

bool QWSDLParser::readTypes(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		logParser("processing: " + xmlReader.name().toString());

		if (xmlReader.name().toString() == TAG_SCHEMA) {
			bRes = readSchema(xmlReader);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readMessage(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	m_pCurrentMessage = Message::create();
	if(xmlAttrs.hasAttribute(ATTR_NAME)) {
		m_pCurrentMessage->setLocalName(xmlAttrs.value(ATTR_NAME).toString());
		m_pCurrentMessage->setNamespace(m_szCurrentTargetNamespacePrefix);
		m_pCurrentMessage->setNamespaceUri(m_szCurrentTargetNamespaceUri);
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		logParser("processing: " + xmlReader.name().toString());

		if (xmlReader.name().toString() == TAG_PART) {
			bRes = readPart(xmlReader);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	// End element
	m_pListMessages->append(m_pCurrentMessage);
	m_pCurrentMessage.clear();

	return bRes;
}

bool QWSDLParser::readPortType(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		logParser("processing: " + xmlReader.name().toString());

		if (xmlReader.name().toString() == TAG_OPERATION) {
			bRes = readOperation(xmlReader);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readBinding(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		logParser("processing: " + xmlReader.name().toString());

		if (xmlReader.name().toString() == TAG_OPERATION) {
			bRes = readOperation(xmlReader);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readPart(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	if(m_pCurrentMessage){
		if(xmlAttrs.hasAttribute(ATTR_NAME) && xmlAttrs.hasAttribute(ATTR_ELEMENT)) {
			if(xmlAttrs.value(ATTR_NAME).toString() == "parameters") {
				QString qualifiedName = xmlAttrs.value(ATTR_ELEMENT).toString();
				if(qualifiedName.contains(":")) {
					RequestResponseElementSharedPtr pElement;
					pElement = m_pListRequestResponseElements->getByName(qualifiedName.split(":")[1], qualifiedName.split(":")[0]);
					if(pElement){
						m_pCurrentMessage->setParameter(pElement);
					}else{
						qWarning("[QWSDLParser] Element not found '%s' for part", qPrintable(qualifiedName));
					}
				}
			}
		}
	}

	// Skip sub elements
	xmlReader.skipCurrentElement();

	return bRes;
}

bool QWSDLParser::readOperation(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QString qName = xmlReader.qualifiedName().toString();

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	if(qName.startsWith("soap:")){
		OperationSharedPtr pOperation = m_pListOperations->getByName(m_szCurrentOperationName);
		if(pOperation) {
			m_pListOperations->removeAll(pOperation);
			pOperation->setSoapAction(xmlAttrs.value("soapAction").toString());
			m_pListOperations->append(pOperation);
			m_szCurrentOperationName = "";
		 }
	}else if(isWSDLSchema(qName)){
		if(xmlAttrs.hasAttribute(ATTR_NAME)) {
			m_szCurrentOperationName = xmlAttrs.value(ATTR_NAME).toString();
			OperationSharedPtr pOperation = m_pListOperations->getByName(m_szCurrentOperationName);
			if(pOperation.isNull()) {
				m_pCurrentOperation = Operation::create();
				m_pCurrentOperation->setName(m_szCurrentOperationName);
			}
		}
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		logParser("processing: " + xmlReader.name().toString());

		if (xmlReader.name().toString() == TAG_INPUT) {
			bRes = readInput(xmlReader);
		}else if (xmlReader.name().toString() == TAG_OUTPUT) {
			bRes = readOutput(xmlReader);
		}else if (xmlReader.name().toString() == TAG_OPERATION) {
			bRes = readOperation(xmlReader);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	// End element
	if(isWSDLSchema(qName)){
		if(m_pCurrentOperation) {
			m_pListOperations->append(m_pCurrentOperation);
			m_pService->addOperation(m_pCurrentOperation);
			m_pCurrentOperation.clear();
		}
	}

	return bRes;
}

bool QWSDLParser::readInput(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	if(m_pCurrentOperation){
		if(xmlAttrs.hasAttribute(ATTR_MESSAGE)) {
			QString qualifiedName = xmlAttrs.value(ATTR_MESSAGE).toString();
			if(qualifiedName.contains(":")) {
				MessageSharedPtr pMessage;
				pMessage = m_pListMessages->getByName(qualifiedName.split(":")[1], qualifiedName.split(":")[0]);
				m_pCurrentOperation->setInputMessage(pMessage);
			}
		}
	}

	// Skip sub elements
	xmlReader.skipCurrentElement();

	return bRes;
}

bool QWSDLParser::readOutput(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	if(m_pCurrentOperation){
		if(xmlAttrs.hasAttribute(ATTR_MESSAGE)) {
			QString qualifiedName = xmlAttrs.value(ATTR_MESSAGE).toString();
			if(qualifiedName.contains(":")) {
				MessageSharedPtr pMessage;
				pMessage = m_pListMessages->getByName(qualifiedName.split(":")[1], qualifiedName.split(":")[0]);
				m_pCurrentOperation->setOutputMessage(pMessage);
			}
		}
	}

	// Skip sub elements
	xmlReader.skipCurrentElement();

	return bRes;
}

bool QWSDLParser::readSchema(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QString szQualifiedName = xmlReader.qualifiedName().toString();
	m_szCurrentSchemaNamespacePrefix = szQualifiedName.split(":")[0];

	// Read XMLS namespaces
	bRes = readXMLNamespaces(xmlReader);

	// Parse attributes
	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();
	QString szTargetNamespaceUri;
	if(xmlAttrs.hasAttribute(ATTR_TARGET_NAMESPACE)) {
		szTargetNamespaceUri = xmlAttrs.value(ATTR_TARGET_NAMESPACE).toString();

		// This is a special case used to find the SOAP standard fault type
		if(szTargetNamespaceUri == "http://www.w3.org/2003/05/soap-envelope"){
			m_bWaitForSoapEnvelopeFault = true;
		}

		m_pService->setTargetNamespace(szTargetNamespaceUri);
	}

	// Enter target namespace if any
	if(!szTargetNamespaceUri.isEmpty()){
		bRes = pushCurrentTargetNamespace(szTargetNamespaceUri);
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if(isComposition(szTagName)){
			bRes = readComposition(xmlReader, szTagName);
		}else if (xmlReader.name().toString() == TAG_SIMPLE_TYPE) {
			bRes = readSimpleType(xmlReader, Section::Schema);
		}else if (xmlReader.name().toString() == TAG_COMPLEX_TYPE) {
			bRes = readComplexType(xmlReader, Section::Schema);
		}else if (xmlReader.name().toString() == TAG_ELEMENT) {
			bRes = readElement(xmlReader, Section::Schema);
		}else if (xmlReader.name().toString() == TAG_ATTRIBUTE) {
			bRes = readAttribute(xmlReader, Section::Schema);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	// Exit target namespace if any
	if(!szTargetNamespaceUri.isEmpty()){
		popCurrentTargetNamespace();
	}

	return bRes;
}

bool QWSDLParser::readComplexType(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	ComplexTypeSharedPtr pComplexType;

	logParser("SJH - readComplexType entered ");

	if(iParentSection == Section::Element)
	{
		pComplexType = ComplexType::create();
		if(m_pCurrentElement){
			pComplexType->setLocalName(m_pCurrentElement->getName());
			pComplexType->setNamespace(m_szCurrentTargetNamespacePrefix);
			pComplexType->setNamespaceUri(m_szCurrentTargetNamespaceUri);
			m_pCurrentElement->setType(pComplexType);
			logParser("SJH - complex type created from element : " + pComplexType->getLocalName());
		}
	}else{
		if(xmlAttrs.hasAttribute(ATTR_NAME))
		{
			TypeSharedPtr pFoundType;
			QString szName = xmlAttrs.value(ATTR_NAME).toString();

			pFoundType = getTypeByName(szName, m_szCurrentTargetNamespacePrefix);
			if(!pFoundType.isNull()) {
				if(pFoundType->getTypeMode() == Type::TypeUnknown) {
					pComplexType = ComplexType::create();
					pComplexType->setLocalName(szName);
					pComplexType->setNamespace(m_szCurrentTargetNamespacePrefix);
					pComplexType->setNamespaceUri(m_szCurrentTargetNamespaceUri);
					logParser("SJH - complex type created from attribute - unknown type found: " + pComplexType->getLocalName());
				}else{
					pComplexType = qSharedPointerCast<ComplexType>(pFoundType);;
					logParser("SJH - complex type created from attribute - complex type found: " + pComplexType->getLocalName());
				}
			}else{
				pComplexType = ComplexType::create();
				pComplexType->setLocalName(szName);
				pComplexType->setNamespace(m_szCurrentTargetNamespacePrefix);
				pComplexType->setNamespaceUri(m_szCurrentTargetNamespaceUri);
				logParser("SJH - complex type created from attribute - type not found: " + pComplexType->getLocalName());
				if ( pComplexType->getLocalName() == "ServiceItem" && pComplexType->getNamespace() == "ldbt2021")
				{
					logParser("SJH - ServiceItem Trap ");
				}
			}

			if((szName == "Fault") && m_bWaitForSoapEnvelopeFault){
				pComplexType->setIsSoapEnvelopeFault(true);
			}
			logParser("SJH - complex type created from attribute - namespace : " + pComplexType->getNamespace());
		}
	}
	if(pComplexType){
		pushCurrentType(pComplexType);
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("SJH - processing sub elements: " + szTagName);
		if (xmlReader.name().toString() == TAG_COMPLEX_CONTENT) {
			bRes = readComplexContent(xmlReader, Section::ComplexType);
			logParser("SJH - sub element is complex: " + szTagName);
		}else if (xmlReader.name().toString() == TAG_SIMPLE_CONTENT) {
			bRes = readSimpleContent(xmlReader, Section::ComplexType);
			logParser("SJH - sub element is simple: " + szTagName);
		}else if(isParticleAndAttrs(szTagName)){
			bRes = readParticleAndAttrs(xmlReader, szTagName, Section::ComplexType);
			logParser("SJH - sub element is attr: " + szTagName);
		}else{
			xmlReader.skipCurrentElement();
			logParser("SJH - sub element is skipped: " + szTagName);
		}
	}
	decrLogIndent();

	// End of element
	if(pComplexType){
		if((iParentSection == Section::Element) && m_pCurrentRequestResponseElement) {
			if(m_pCurrentElement && (m_pCurrentElement->getName() != m_pCurrentRequestResponseElement->getLocalName())){
				m_pListTypes->add(pComplexType);
			}

			m_pCurrentRequestResponseElement->setType(pComplexType);
		}else{
			m_pListTypes->add(pComplexType);
		}

		if(m_pCurrentAttribute){
			m_pCurrentAttribute->setType(pComplexType);
		}

		popCurrentType();
	}

	logParser("SJH - readComplexType exited ");
	if ( pComplexType->getLocalName() == "ServiceItem")
	{
		logParser("SJH - ServiceItem Trap ");
	}

	return bRes;
}

bool QWSDLParser::readComplexContent(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_RESTRICTION) {
			bRes = readRestriction(xmlReader, iParentSection);
		}else if (xmlReader.name().toString() == TAG_EXTENSION) {
			bRes = readExtension(xmlReader, iParentSection);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readSimpleContent(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_RESTRICTION) {
			bRes = readRestriction(xmlReader, iParentSection);
		}else if (xmlReader.name().toString() == TAG_EXTENSION) {
			bRes = readExtension(xmlReader, iParentSection);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readExtension(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	if(xmlAttrs.hasAttribute(ATTR_BASE)){
		QString szName = xmlAttrs.value(ATTR_BASE).toString();

		TypeSharedPtr pType = getTypeByName(szName.split(":")[1], szName.split(":")[0]);
		if(!pType.isNull()){
			qSharedPointerCast<ComplexType>(pCurrentType)->setExtensionType(pType);
		}else{
			if(szName.startsWith(m_szCurrentSchemaNamespacePrefix + ":")) {
				SimpleTypeSharedPtr pType = SimpleType::create();
				pType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szName);
				qSharedPointerCast<ComplexType>(pCurrentType)->setExtensionType(pType);
			}else{
				TypeSharedPtr pType = Type::create();
				pType->setNamespace(szName.split(":")[0]);
				pType->setLocalName(szName.split(":")[1]);
				m_pListTypes->append(pType);

				qSharedPointerCast<ComplexType>(pCurrentType)->setExtensionType(pType);
			}
		}
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (isParticleAndAttrs(szTagName)) {
			bRes = readParticleAndAttrs(xmlReader, szTagName, iParentSection);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readElement(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pTypeFound;
	ElementSharedPtr pPreviousElement;
	RequestResponseElementSharedPtr pRequestResponseElement;

	// Read attributes
	QString szName;
	if(xmlAttrs.hasAttribute(ATTR_NAME)) {
		szName = xmlAttrs.value(ATTR_NAME).toString();
	}
	QString szRef;
	if(xmlAttrs.hasAttribute(ATTR_REF)) {
		szRef = xmlAttrs.value(ATTR_REF).toString();
	}

	if(iParentSection == Section::ComplexType){
		TypeSharedPtr pCurrentType = getCurrentType();

		ElementSharedPtr pElement = Element::create();
		pPreviousElement = m_pCurrentElement;
		m_pCurrentElement = pElement;

		if(!szRef.isEmpty()){
			ElementSharedPtr pRefElement = getElementByRef(szRef);
			if(pRefElement){
				pElement->setRef(pRefElement);
			}else{
				pElement->setRefValue(szRef);
			}
		}else{
			pElement->setName(szName);

			if(xmlAttrs.hasAttribute(ATTR_TYPE)) {
				QString szValue = xmlAttrs.value(ATTR_TYPE).toString();
				QString szNamespace = szValue.split(":")[0];
				QString szLocalName = szValue.split(":")[1];

				pTypeFound = getTypeByName(szLocalName, szNamespace);
				if(!pTypeFound.isNull()){
					pElement->setType(pTypeFound);
				}else{
					qWarning("[QWSDLParser] Type %s is not found at this moment, we create it", qPrintable(szValue));

					if(szValue.startsWith(m_szCurrentSchemaNamespacePrefix + ":")) {
						SimpleTypeSharedPtr pSimpleType = SimpleType::create();
						pSimpleType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szValue);
						pSimpleType->setName(pElement->getName());
						pTypeFound = pSimpleType;
					}else{
						pTypeFound = Type::create();
						pTypeFound->setLocalName(szLocalName);
						pTypeFound->setNamespace(szNamespace);
					}
					pElement->setType(pTypeFound);
				}

				if(pElement->getType()->getNameWithNamespace() == pCurrentType->getNameWithNamespace()){
					pElement->setIsNested(true);
				}
			}
		}
		//			else {
		//				if(attributes.index(ATTR_REF) != -1) {
		//					QString szValue = attributes.value(ATTR_REF);
		//					QString szNamespace = szValue.split(":")[0];
		//					QString szLocalName = szValue.split(":")[1];
		//
		//					TypeSharedPtr pType = getTypeByName(szLocalName, szNamespace);
		//					if(!pType.isNull()){
		//						element->setType(pType);
		//					}else{
		//						qWarning("[QWSDLParser] Type %s is not found at this moment, we create it", qPrintable(szValue));
		//
		//						if(szValue.startsWith(m_szCurrentElementNamespacePrefix + ":")) {
		//							SimpleTypeSharedPtr pType = SimpleType::create();
		//							pType->setVariableTypeFromString(m_szCurrentElementNamespacePrefix, szValue);
		//							pType->setName(element->getName());
		//							element->setType(pType);
		//						}else{
		//							TypeSharedPtr pType = Type::create();
		//							pType->setLocalName(szLocalName);
		//							pType->setNamespace(szNamespace);
		//							m_pListTypes->append(pType);
		//							element->setType(pType);
		//						}
		//					}
		//				}
		//			}

		if(xmlAttrs.hasAttribute(ATTR_MIN_OCCURS)) {
			QString szValue = xmlAttrs.value(ATTR_MIN_OCCURS).toString();
			pElement->setMinOccurs(szValue.toUInt());
		}

		if(xmlAttrs.hasAttribute(ATTR_MAX_OCCURS)) {
			QString szValue = xmlAttrs.value(ATTR_MAX_OCCURS).toString();
			pElement->setMaxOccurs(szValue == "unbounded" ? -1 : szValue.toUInt());
		}

		ComplexTypeSharedPtr pComplexType = qSharedPointerCast<ComplexType>(pCurrentType);
		pComplexType->addElement(pElement);
	}else{
		if(iParentSection == Section::Schema){
			//We are in Message Request/Response elements list section
			pRequestResponseElement = RequestResponseElement::create();
			pRequestResponseElement->setLocalName(szName);
			pRequestResponseElement->setNamespace(m_szCurrentTargetNamespacePrefix);
			pRequestResponseElement->setNamespaceUri(m_szCurrentTargetNamespaceUri);
			m_pCurrentRequestResponseElement = pRequestResponseElement;
		}

		m_pCurrentElement = ElementSharedPtr::create();
		m_pCurrentElement->setNamespace(m_szCurrentTargetNamespacePrefix);
		m_pCurrentElement->setName(szName);

		if(xmlAttrs.hasAttribute(ATTR_TYPE))
		{
			QString szValue = xmlAttrs.value(ATTR_TYPE).toString();
			QString szNamespace = szValue.split(":")[0];
			QString szLocalName = szValue.split(":")[1];
			pTypeFound = getTypeByName(szLocalName, szNamespace);
			if(!pTypeFound.isNull()){
				m_pCurrentElement->setType(pTypeFound);
			}else{
				qWarning("[QWSDLParser] Type %s is not found at this moment, we create it", qPrintable(szValue));

				if(szValue.startsWith(m_szCurrentSchemaNamespacePrefix + ":")) {
					SimpleTypeSharedPtr pType = SimpleType::create();
					pType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szValue);
					pType->setName(m_pCurrentElement->getName());
					pType->setNamespace(m_szCurrentTargetNamespacePrefix);
					pType->setNamespaceUri(m_szCurrentTargetNamespaceUri);
					pTypeFound = pType;
				}else{
					pTypeFound = Type::create();
					pTypeFound->setLocalName(szLocalName);
					pTypeFound->setNamespace(szNamespace);
				}
				m_pCurrentElement->setType(pTypeFound);
			}
		}
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_SIMPLE_TYPE) {
			bRes = readSimpleType(xmlReader, Section::Element);
		}else if (xmlReader.name().toString() == TAG_COMPLEX_TYPE) {
			bRes = readComplexType(xmlReader, Section::Element);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	if(m_pCurrentElement){
		m_pListElements->append(m_pCurrentElement);
		m_pCurrentElement.clear();
	}
	m_pCurrentElement = pPreviousElement;

	if(pRequestResponseElement){
		if(pTypeFound){
			pRequestResponseElement->setType(pTypeFound);
		}
		m_pListRequestResponseElements->append(pRequestResponseElement);
		m_pCurrentRequestResponseElement.clear();
	}

	return bRes;
}

bool QWSDLParser::isGroup(const QString& szTagName) const
{
	if(szTagName == TAG_GROUP){
		return true;
	}
	return false;
}

bool QWSDLParser::readGroup(QXmlStreamReader& xmlReader, const QString& szTagName, Section::Name iParentSection)
{
	if(isMgs(szTagName)){
		return readMsg(xmlReader, szTagName, iParentSection);
	}
	return true;
}

bool QWSDLParser::readSequence(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_ELEMENT) {
			bRes = readElement(xmlReader, iParentSection);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readAny(QXmlStreamReader& xmlReader)
{
	// https://www.w3schools.com/xml/el_any.asp

	// Hack: we consider using simple type xs:string to allow developer to choose the proper value in use.
	// Only do it for empty type (never exists otherwise)
	//		if(inSection(Section::ComplexType) && currentType()){
	//			ComplexTypeSharedPtr pComplexType = qSharedPointerCast<ComplexType>(currentType());
	//			if(!pComplexType->getExtensionType() && pComplexType->getElementList()->isEmpty()){
	//				SimpleTypeSharedPtr pSimpleType = SimpleType::create();
	//				pSimpleType->setVariableType(SimpleType::AnyType);
	//				pComplexType->setExtensionType(pSimpleType);
	//			}
	//		}


	// Skip sub element
	xmlReader.skipCurrentElement();

	return true;
}

bool QWSDLParser::isAttribute(const QString& szTagName) const
{
	if(szTagName == TAG_ATTRIBUTE){
		return true;
	}
	return false;
}

bool QWSDLParser::readAttribute(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	QString szName;
	if(xmlAttrs.hasAttribute(ATTR_NAME)) {
		szName = xmlAttrs.value(ATTR_NAME).toString();
	}

	if((iParentSection == Section::ComplexType) && pCurrentType){
		QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

		AttributeSharedPtr attr = Attribute::create();

		if(xmlAttrs.hasAttribute(ATTR_REF)){
			AttributeSharedPtr pRefAttr = m_pListAttributes->getByRef(xmlAttrs.value(ATTR_REF).toString());
			if(pRefAttr){
				attr->setRef(pRefAttr);
			}
		}else{
			if(!szName.isEmpty()) {
				attr->setName(szName);
			}

			if(xmlAttrs.hasAttribute(ATTR_TYPE)) {
				QString szValue = xmlAttrs.value(ATTR_TYPE).toString();
				QString szNamespace = szValue.split(":")[0];
				QString szLocalName = szValue.split(":")[1];
				TypeSharedPtr pType = getTypeByName(szLocalName, szNamespace);
				if(!pType.isNull()){
					attr->setType(pType);
				}else{
					qWarning("[QWSDLParser] Type %s is not found at this moment, we create it", qPrintable(szValue));

					if(szValue.startsWith(m_szCurrentSchemaNamespacePrefix + ":")) {
						SimpleTypeSharedPtr pType = SimpleType::create();
						pType->setName(attr->getName());
						pType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szValue);
						attr->setType(pType);
					}else{
						TypeSharedPtr pType = Type::create();
						pType->setLocalName(szLocalName);
						pType->setNamespace(szNamespace);
						m_pListTypes->append(pType);
						attr->setType(pType);
					}
				}
			}else{
				m_pCurrentAttribute = attr;
				m_pCurrentAttribute->setNamespace(m_szCurrentTargetNamespacePrefix);
			}
		}

		if(xmlAttrs.hasAttribute(ATTR_USE)) {
			QString szValue = xmlAttrs.value(ATTR_USE).toString();
			if(szValue == "required") {
				attr->setRequired(true);
			}
		}

		ComplexTypeSharedPtr pComplexType = qSharedPointerCast<ComplexType>(pCurrentType);
		pComplexType->addAttribute(attr);
	}else{
		m_pCurrentAttribute = AttributeSharedPtr::create();
		m_pCurrentAttribute->setNamespace(m_szCurrentTargetNamespacePrefix);
		if(!szName.isEmpty()){
			m_pCurrentAttribute->setName(szName);
		}

		if(xmlAttrs.hasAttribute(ATTR_TYPE)) {
			QString szValue = xmlAttrs.value(ATTR_TYPE).toString();
			QString szNamespace = szValue.split(":")[0];
			QString szLocalName = szValue.split(":")[1];
			TypeSharedPtr pType = getTypeByName(szLocalName, szNamespace);
			if(!pType.isNull()){
				m_pCurrentAttribute->setType(pType);
			}else{
				qWarning("[QWSDLParser] Type %s is not found at this moment, we create it", qPrintable(szValue));

				if(szValue.startsWith(m_szCurrentSchemaNamespacePrefix + ":")) {
					SimpleTypeSharedPtr pType = SimpleType::create();
					pType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szValue);
					pType->setName(m_pCurrentAttribute->getName());
					m_pCurrentAttribute->setType(pType);
				}else{
					TypeSharedPtr pType = Type::create();
					pType->setLocalName(szLocalName);
					pType->setNamespace(szNamespace);
					m_pCurrentAttribute->setType(pType);
				}
			}
		}
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_SIMPLE_TYPE) {
			bRes = readSimpleType(xmlReader, Section::Attribute);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	if(m_pCurrentAttribute){
		m_pListAttributes->append(m_pCurrentAttribute);
		m_pCurrentAttribute.clear();
	}

	return bRes;
}

bool QWSDLParser::isAttributeGroup(const QString& szTagName) const
{
	if(szTagName == TAG_ATTRIBUTE_GROUP){
		return true;
	}
	return false;
}

bool QWSDLParser::readAttributeGroup(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_ATTRIBUTE) {
			bRes = readAttribute(xmlReader, iParentSection);
		}else if (xmlReader.name().toString() == TAG_ATTRIBUTE_GROUP) {
			bRes = readAttributeGroup(xmlReader, iParentSection);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::isComposition(const QString& szTagName) const
{
	if(szTagName == TAG_INCLUDE){
		return true;
	}
	if(szTagName == TAG_IMPORT){
		return true;
	}
	if(szTagName == TAG_OVERRIDE){
		return true;
	}
	if(szTagName == TAG_REDEFINE){
		return true;
	}
	return false;
}

bool QWSDLParser::readComposition(QXmlStreamReader& xmlReader, const QString& szTagName)
{
	if(szTagName == TAG_INCLUDE){
		return readInclude(xmlReader);
	}else if(szTagName == TAG_IMPORT){
		return readImport(xmlReader);
	}else{
		xmlReader.skipCurrentElement();
	}
	return true;
}

bool QWSDLParser::readInclude(QXmlStreamReader& xmlReader)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	QString szLocation;
	QString szNamespaceUri;
	if(xmlAttrs.hasAttribute(ATTR_SCHEMA_LOCATION))
	{
		szLocation = "./wsdl/" + xmlAttrs.value(ATTR_SCHEMA_LOCATION).toString();
	}
	if(xmlAttrs.hasAttribute(ATTR_NAMESPACE))
	{
		szNamespaceUri = xmlAttrs.value(ATTR_NAMESPACE).toString();
	}

	if(!szLocation.isEmpty()){
		logParser("starting import: " + szLocation);
		incrLogIndent();

		if(szNamespaceUri.isEmpty()){
			szNamespaceUri = m_szCurrentTargetNamespaceUri;
		}

		QString szRemoteLocation;
		if(szLocation.startsWith("http://") || szLocation.startsWith("https://")) {
			// Use URL
			szRemoteLocation = szLocation;
		}else if(!szNamespaceUri.isEmpty()){
			// Build URL from current namespace URI
			if(szNamespaceUri.startsWith("http://") || szNamespaceUri.startsWith("https://"))
			{
				szRemoteLocation = szNamespaceUri + (szNamespaceUri.endsWith("/") ? szLocation : ("/" + szLocation));
			}
		}

		if(!szRemoteLocation.isEmpty()){
			bRes = loadFromHttp(szRemoteLocation, szNamespaceUri);
		}else{
			bRes = false;
		}
		if(!bRes){
			bRes = loadFromFile(szLocation, szNamespaceUri);
		}

		decrLogIndent();
		logParser("end of import");
	}

	// Skip sub elements
	xmlReader.skipCurrentElement();

	return bRes;
}

bool QWSDLParser::readImport(QXmlStreamReader& xmlReader)
{
	// Do the same as include
	return readInclude(xmlReader);
}


// Mgs
bool QWSDLParser::isMgs(const QString& szTagName) const
{
	if(szTagName == TAG_ALL){
		return true;
	}
	if(szTagName == TAG_CHOICE){
		return true;
	}
	if(szTagName == TAG_SEQUENCE){
		return true;
	}
	return false;
}

bool QWSDLParser::readMsg(QXmlStreamReader& xmlReader, const QString& szTagName, Section::Name iParentSection)
{
	if(szTagName == TAG_SEQUENCE){
		return readSequence(xmlReader, iParentSection);
	}else{
		xmlReader.skipCurrentElement();
	}
	return true;
}

bool QWSDLParser::isAttrDecls(const QString& szTagName) const
{
	if(isAttribute(szTagName)){
		return true;
	}
	if(isAttributeGroup(szTagName)){
		return true;
	}
	return false;
}

bool QWSDLParser::readAttrDecls(QXmlStreamReader& xmlReader, const QString& szTagName, Section::Name iParentSection)
{
	if(isAttribute(szTagName)){
		return readAttribute(xmlReader, iParentSection);
	}
	if(isAttributeGroup(szTagName)){
		return readAttributeGroup(xmlReader, iParentSection);
	}

	xmlReader.skipCurrentElement();

	return true;
}

bool QWSDLParser::isParticleAndAttrs(const QString& szTagName) const
{
	if(isMgs(szTagName)){
		return true;
	}
	if(isGroup(szTagName)){
		return true;
	}
	if(isAttrDecls(szTagName)){
		return true;
	}
	return false;
}

bool QWSDLParser::readParticleAndAttrs(QXmlStreamReader& xmlReader, const QString& szTagName, Section::Name iParentSection)
{
	if(isMgs(szTagName)){
		return readMsg(xmlReader, szTagName, iParentSection);
	}
	if(isGroup(szTagName)){
		return readGroup(xmlReader, szTagName, iParentSection);
	}
	if(isAttrDecls(szTagName)){
		return readAttrDecls(xmlReader, szTagName, iParentSection);
	}
	xmlReader.skipCurrentElement();
	return true;
}

bool QWSDLParser::readSimpleType(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();
	QString szName;

	SimpleTypeSharedPtr pSimpleType;

	if((iParentSection == Section::Attribute) && m_pCurrentAttribute && (xmlAttrs.count() == 0)){
		TypeSharedPtr pCurrentType = SimpleType::create();
		m_pCurrentAttribute->setType(pCurrentType);
	}

	if(xmlAttrs.hasAttribute(ATTR_NAME)){
		szName = szName = xmlAttrs.value(ATTR_NAME).toString();
	}

	if ( iParentSection == Section::Element && szName.isEmpty())
	{
		szName = m_pCurrentElement->getName();
	}

	if(!szName.isEmpty()){
		TypeSharedPtr pFoundType = getTypeByName(szName, m_szCurrentTargetNamespacePrefix);
		if(!pFoundType.isNull()) {
			if(pFoundType->getTypeMode() == Type::TypeUnknown) {
				pSimpleType = SimpleType::create();
			}
		}else{
			pSimpleType = SimpleType::create();
		}
		if(pSimpleType){
			pSimpleType->setLocalName(szName);
			pSimpleType->setNamespace(m_szCurrentTargetNamespacePrefix);
			pSimpleType->setNamespaceUri(m_szCurrentTargetNamespaceUri);
		}
	}
	if(pSimpleType){
		pushCurrentType(pSimpleType);
	}

	bool bList = false;

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_RESTRICTION) {
			bRes = readRestriction(xmlReader, Section::SimpleType);
		}else if (xmlReader.name().toString() == TAG_LIST) {
			bList = true;
			bRes = readList(xmlReader, (szName.isNull() ? iParentSection : Section::SimpleType));
		}else if (xmlReader.name().toString() == TAG_UNION) {
			bRes = readUnion(xmlReader, Section::SimpleType);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	// End of element
	if(pSimpleType)
	{
		// This is replaced with a complex type if type is list
		if(!bList){
			m_pListTypes->add(qSharedPointerCast<Type>(pSimpleType));
		}

		if(m_pCurrentAttribute){
			m_pCurrentAttribute->setType(pSimpleType);
		}

		popCurrentType();
	}

	return bRes;
}

bool QWSDLParser::readRestriction(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	if((iParentSection == Section::SimpleType) &&
	(pCurrentType || (m_pCurrentAttribute &&
						  m_pCurrentAttribute->getType() &&
						  (m_pCurrentAttribute->getType()->getTypeMode() == Type::TypeSimple))))
	{
		SimpleTypeSharedPtr pSimpleType;
		if(pCurrentType){
			pSimpleType = qSharedPointerCast<SimpleType>(pCurrentType);
		}else{
			pSimpleType = qSharedPointerCast<SimpleType>(m_pCurrentAttribute->getType());
			if(pSimpleType){
				pSimpleType->setName(m_pCurrentAttribute->getName());
			}
		}

		if(pSimpleType){
			if(xmlAttrs.hasAttribute(ATTR_BASE)) {
				QString szValue = xmlAttrs.value(ATTR_BASE).toString();

				if(szValue.startsWith(m_szCurrentSchemaNamespacePrefix + ":")) {
					pSimpleType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szValue);
				}else{
					pSimpleType->setVariableType(SimpleType::Custom);
					pSimpleType->setCustomNamespace(szValue.split(":")[0].toUpper());
					pSimpleType->setCustomName(szValue.split(":")[1]);
				}
				pSimpleType->setRestricted(true);
			}
		}
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_SIMPLE_TYPE) {
			bRes = readSimpleType(xmlReader, iParentSection);
		}else if(isFacet(szTagName)){
			bRes = readFacet(xmlReader, szTagName);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readList(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	if((iParentSection == Section::Attribute) && m_pCurrentAttribute)
	{
		if(xmlAttrs.hasAttribute(ATTR_ITEM_TYPE)){
			QString szValue = xmlAttrs.value(ATTR_ITEM_TYPE).toString();
			SimpleTypeSharedPtr pType = SimpleType::create();
			pType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szValue);
			m_pCurrentAttribute->setType(pType);
			m_pCurrentAttribute->setIsList(true);
		}
	}else if((iParentSection == Section::SimpleType) && pCurrentType)
	{
		if(xmlAttrs.hasAttribute(ATTR_ITEM_TYPE)){
			//				 We consider representing (for example):
			//
			//				<xs:simpleType name="IntAttrList">
			//					<xs:list itemType="xs:int"/>
			//				</xs:simpleType>
			//
			//				by:
			//				class IntAttrList : public QList<XS::Int> ...
			//
			//				So we admit it's a complex type with an extension. We remove the existing current type to build a new one.

			QString szValue = xmlAttrs.value(ATTR_ITEM_TYPE).toString();
			QString szLocalName = pCurrentType->getLocalName();
			QString szNamespace = pCurrentType->getNamespace();

			ComplexTypeSharedPtr pComplexType = ComplexType::create();
			pComplexType->setLocalName(szLocalName);
			pComplexType->setNamespace(szNamespace);

			TypeSharedPtr pType = getTypeByName(szValue.split(":")[1], szValue.split(":")[0]);
			if(!pType.isNull()){
				pComplexType->setExtensionType(pType, true);
			}else{
				if(szValue.startsWith(m_szCurrentSchemaNamespacePrefix + ":")) {
					SimpleTypeSharedPtr pType = SimpleType::create();
					pType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szValue);
					pComplexType->setExtensionType(pType, true);
				}else{
					TypeSharedPtr pType = Type::create();
					pType->setNamespace(szValue.split(":")[0]);
					pType->setLocalName(szValue.split(":")[1]);

					pComplexType->setExtensionType(pType, true);
				}
			}

			m_pListTypes->append(pComplexType);
		}
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_SIMPLE_TYPE) {
			bRes = readSimpleType(xmlReader, iParentSection);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readUnion(QXmlStreamReader& xmlReader, Section::Name iParentSection)
{
	bool bRes = true;

	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	// For now, we consider using only the primitive simple type in the union.
	// Example: <xs:union memberTypes="tns:RelationshipType xs:anyURI"/> --> We consider only the xs:anyURI.
	if((iParentSection == Section::SimpleType) && pCurrentType){
		if(xmlAttrs.hasAttribute(ATTR_MEMBER_TYPES)){
			QString szMemberTypes = xmlAttrs.value(ATTR_MEMBER_TYPES).toString();
			QStringList szTypes = szMemberTypes.split(" ");
			for(int i = 0; i < szTypes.size(); ++i){
				if(szTypes[i].startsWith(m_szCurrentSchemaNamespacePrefix + ":")){
					SimpleTypeSharedPtr pSimpleType = qSharedPointerCast<SimpleType>(pCurrentType);
					pSimpleType->setVariableTypeFromString(m_szCurrentSchemaNamespacePrefix, szTypes[i]);
				}
			}
		}
	}

	// Read sub elements
	incrLogIndent();
	while (bRes && xmlReader.readNextStartElement())
	{
		QString szTagName = xmlReader.name().toString();
		logParser("processing: " + szTagName);

		if (xmlReader.name().toString() == TAG_SIMPLE_TYPE) {
			bRes = readSimpleType(xmlReader, iParentSection);
		}else{
			xmlReader.skipCurrentElement();
		}
	}
	decrLogIndent();

	return bRes;
}

bool QWSDLParser::readMaxLength(QXmlStreamReader& xmlReader)
{
	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	if(pCurrentType){
		if(xmlAttrs.hasAttribute(ATTR_VALUE)) {
			SimpleTypeSharedPtr pSimpleType = qSharedPointerCast<SimpleType>(pCurrentType);
			QString szValue = xmlAttrs.value(ATTR_VALUE).toString();
			pSimpleType->setMaxLength(szValue.toUInt());
		}
	}

	// Skip sub element parsing
	xmlReader.skipCurrentElement();

	return true;
}

bool QWSDLParser::readMinLength(QXmlStreamReader& xmlReader)
{
	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	if(pCurrentType){
		if(xmlAttrs.hasAttribute(ATTR_VALUE)) {
			SimpleTypeSharedPtr pSimpleType = qSharedPointerCast<SimpleType>(pCurrentType);
			QString szValue = xmlAttrs.value(ATTR_VALUE).toString();
			pSimpleType->setMinLength(szValue.toUInt());
		}
	}

	// Skip sub element parsing
	xmlReader.skipCurrentElement();

	return true;
}

bool QWSDLParser::readEnumeration(QXmlStreamReader& xmlReader)
{
	QXmlStreamAttributes xmlAttrs = xmlReader.attributes();

	TypeSharedPtr pCurrentType = getCurrentType();

	if(pCurrentType){
		if(xmlAttrs.hasAttribute(ATTR_VALUE)) {
			SimpleTypeSharedPtr pSimpleType = qSharedPointerCast<SimpleType>(pCurrentType);
			pSimpleType->addEnumerationValue(xmlAttrs.value(ATTR_VALUE).toString());
		}
	}

	// Skip sub element parsing
	xmlReader.skipCurrentElement();

	return true;
}

bool QWSDLParser::isFacet(const QString& szTagName)
{
	if(szTagName == TAG_MAX_LENGTH){
		return true;
	}
	if(szTagName == TAG_MIN_LENGTH){
		return true;
	}
	if(szTagName == TAG_ENUMERATION){
		return true;
	}
	return false;
}

bool QWSDLParser::readFacet(QXmlStreamReader& xmlReader, const QString& szTagName)
{
	if(szTagName == TAG_MAX_LENGTH){
		return readMaxLength(xmlReader);
	}
	if(szTagName == TAG_MIN_LENGTH){
		return readMinLength(xmlReader);
	}
	if(szTagName == TAG_ENUMERATION){
		return readEnumeration(xmlReader);
	}

	// Skip sub element
	xmlReader.skipCurrentElement();

	return true;
}

TypeListSharedPtr QWSDLParser::getTypeList() const
{
	return m_pListTypes;
}

RequestResponseElementListSharedPtr QWSDLParser::getRequestResponseElementList() const
{
	return m_pListRequestResponseElements;
}

ServiceSharedPtr QWSDLParser::getService() const
{
	return m_pService;
}

AttributeListSharedPtr QWSDLParser::getAttributeList() const
{
	return m_pListAttributes;
}

ElementListSharedPtr QWSDLParser::getElementList() const
{
	return m_pListElements;
}

ElementSharedPtr QWSDLParser::getElementByRef(const QString& szRef)
{
	ElementSharedPtr pElement = m_pListElements->getByRef(szRef);
	if(!pElement){
		QString szNamespace = szRef.split(":")[0];
		QString szName = szRef.split(":")[1];
		if(m_pWSDLData->hasNamespaceDeclaration(szNamespace)){
			QString szNamespaceTmp = m_pWSDLData->getNamespaceDeclaration(szNamespace);
			pElement = m_pListElements->getByRef(szNamespaceTmp + ":" + szName);
		}
	}
	return pElement;
}

TypeSharedPtr QWSDLParser::getTypeByName(const QString& szLocalName, const QString& szNamespace, const TypeListSharedPtr& pListIgnoredTypes)
{
	TypeSharedPtr pType = m_pListTypes->getByName(szLocalName, szNamespace, pListIgnoredTypes);
	if(!pType && m_pWSDLData){
		pType = m_pWSDLData->getTypeByName(szLocalName, szNamespace, pListIgnoredTypes);
	}
	if(!pType && m_pWSDLData->hasNamespaceDeclaration(szNamespace)){
		QString szNamespaceTmp = m_pWSDLData->getNamespaceDeclaration(szNamespace);
		pType = m_pListTypes->getByName(szLocalName, szNamespaceTmp, pListIgnoredTypes);
	}
	return pType;
}

TypeRefSharedPtr QWSDLParser::getTypeRefByTypeName(const QString& szTypeName, const QString& szNamespace)
{
	return m_pWSDLData->getTypeRefByTypeName(szTypeName, szNamespace);
}

bool QWSDLParser::isWSDLSchema(const QString& szQName)
{
	QStringList tokens = szQName.split(":");
	QString szSchema;

	if(tokens.count() == 2){
		szSchema = tokens[0];
	}else if(tokens.count() == 1){
		szSchema = "wsdl";
	}

	return (szSchema == "wsdl");
}

bool QWSDLParser::loadFromHttp(const QString& szURL, const QString& szNamespaceUri)
{
	bool bRes = false;

	if(m_pWSDLData->hasLoadedURI(szURL)){
		logParser("already loaded from http: " + szURL);
		return true;
	}

	logParser("loading from http: " + szURL);
	printf("[QWSDLParser] Loading from http: %s", qPrintable(szURL));

	// Download the file
	QNetworkAccessManager manager;
	QNetworkReply* reply = manager.get(QNetworkRequest(QUrl::fromUserInput(szURL)));
	QEventLoop loop;
	QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
	loop.exec();

	// Handle HTTP 301: redirected url
	QVariant possibleRedirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
	if(!possibleRedirectUrl.isNull()){
		QUrl redirectedUrl = possibleRedirectUrl.toUrl();
		if(!redirectedUrl.isEmpty()){
			reply = manager.get(QNetworkRequest(redirectedUrl));
			QEventLoop loop;
			QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
			loop.exec();
		}
	}

	// Load response
	QByteArray bytes(reply->readAll());

	// Parse WSDL
	QWSDLParser parser;
	parser.setInitialNamespaceUri(szNamespaceUri);
	parser.setLogIndent(m_iLogIndent);
	parser.setWSDLData(m_pWSDLData);
	m_pWSDLData->setTypeList(m_pListTypes);
	QXmlStreamReader xmlReader;
	xmlReader.addData(bytes);
	bRes = parser.parse(xmlReader);
	if(bRes)
	{
		m_pWSDLData->addLoadedURI(szURL);

		TypeListSharedPtr pList = parser.getTypeList();
		TypeList::const_iterator type;
		for(type = pList->constBegin(); type != pList->constEnd(); ++type) {
			m_pListTypes->add(*type);
		}
		AttributeListSharedPtr pListAttributes = parser.getAttributeList();
		AttributeList::const_iterator attribute;
		for(attribute = pListAttributes->constBegin(); attribute != pListAttributes->constEnd(); ++attribute){
			m_pListAttributes->append(*attribute);
		}
		ElementListSharedPtr pListElements = parser.getElementList();
		ElementList::const_iterator element;
		for(element = pListElements->constBegin(); element != pListElements->constEnd(); ++element){
			m_pListElements->append(*element);
		}
		RequestResponseElementListSharedPtr pListRequestResponseElement = parser.getRequestResponseElementList();;
		RequestResponseElementList::const_iterator iter_rre;
		for(iter_rre = pListRequestResponseElement->constBegin(); iter_rre != pListRequestResponseElement->constEnd(); ++iter_rre){
			m_pListRequestResponseElements->append(*iter_rre);
		}
	}else{
		qWarning("[QWSDLParser] Error to parse file %s (error: %s)",
				qPrintable(szURL),
				qPrintable(xmlReader.errorString()));
	}

	printf("[QWSDLParser] End of loading from http: %s", qPrintable(szURL));

	return bRes;
}

bool QWSDLParser::loadFromFile(const QString& szFileName, const QString& szNamespaceUri)
{
	bool bRes = true;

	if(m_pWSDLData->hasLoadedURI(szFileName)){
		logParser("already loaded from file: " + szFileName);
		return true;
	}

	printf("[QWSDLParser] Loading from file: %s", qPrintable(szFileName));

	QFile file(szFileName);
	bRes = file.open(QFile::ReadOnly);
	if(bRes)
	{
		// Load response
		QByteArray bytes(file.readAll());

		// Parse WSDL
		QWSDLParser parser;
		parser.setInitialNamespaceUri(szNamespaceUri);
		parser.setLogIndent(m_iLogIndent);
		parser.setWSDLData(m_pWSDLData);
		m_pWSDLData->setTypeList(m_pListTypes);
		QXmlStreamReader xmlReader;
		xmlReader.addData(bytes);
		bRes = parser.parse(xmlReader);
		if(bRes)
		{
			m_pWSDLData->addLoadedURI(szFileName);

			TypeListSharedPtr pList = parser.getTypeList();
			TypeList::const_iterator type;
			for(type = pList->constBegin(); type != pList->constEnd(); ++type) {
				m_pListTypes->add(*type);
			}
			AttributeListSharedPtr pListAttributes = parser.getAttributeList();
			AttributeList::const_iterator attribute;
			for(attribute = pListAttributes->constBegin(); attribute != pListAttributes->constEnd(); ++attribute){
				m_pListAttributes->append(*attribute);
			}
			ElementListSharedPtr pListElements = parser.getElementList();
			ElementList::const_iterator element;
			for(element = pListElements->constBegin(); element != pListElements->constEnd(); ++element){
				m_pListElements->append(*element);
			}
			RequestResponseElementListSharedPtr pListRequestResponseElement = parser.getRequestResponseElementList();;
			RequestResponseElementList::const_iterator iter_rre;
			for(iter_rre = pListRequestResponseElement->constBegin(); iter_rre != pListRequestResponseElement->constEnd(); ++iter_rre){
				m_pListRequestResponseElements->append(*iter_rre);
			}
		}else{
			qWarning("[QWSDLParser] Error to parse file %s (error: %s)",
					qPrintable(szFileName),
					qPrintable(xmlReader.errorString()));
		}
	}else{
		qWarning("[QWSDLParser] Error for opening file %s (error: %s)",
				qPrintable(szFileName),
				qPrintable(file.errorString()));
	}

	printf("[QWSDLParser] End of loading from file: %s", qPrintable(szFileName));

	return bRes;
}

void QWSDLParser::pushCurrentType(const TypeSharedPtr& pCurrentType)
{
	logParser(" add type: " + pCurrentType->getLocalName() + " at level : " + QString::number(m_stackCurrentTypes.count()));
	m_stackCurrentTypes.push(pCurrentType);
}

void QWSDLParser::popCurrentType()
{
	logParser(" pop type:" + QString::number(m_stackCurrentTypes.count()));
	if ( !m_stackCurrentTypes.empty() )
	{
		m_stackCurrentTypes.pop();
	}
}

const TypeSharedPtr QWSDLParser::getCurrentType() const
{
	return !m_stackCurrentTypes.empty() ? m_stackCurrentTypes.top() : TypeSharedPtr(NULL);
}

bool QWSDLParser::pushCurrentTargetNamespace(const QString& szTargetNamespaceURI)
{
	bool bRes = true;

	m_stackTargetNamespace.push(szTargetNamespaceURI);

	updateTargetNamespacePrefix(szTargetNamespaceURI);

	return bRes;
}

void QWSDLParser::popCurrentTargetNamespace()
{
	QString szTargetNamespace;

	m_stackTargetNamespace.pop();

	if(!m_stackTargetNamespace.isEmpty()){
		szTargetNamespace = m_stackTargetNamespace.top();
	}

	updateTargetNamespacePrefix(szTargetNamespace);
}

void QWSDLParser::updateTargetNamespacePrefix(const QString& szTargetNamespaceURI)
{
	const QWSDLNamespaceDeclarations& listNamespaceDeclarations = m_pWSDLData->getNamespaceDeclarations();

	QMap<QString, QString>::const_iterator iter;
	iter = listNamespaceDeclarations.constBegin();
	while(iter != listNamespaceDeclarations.constEnd())
	{
		const QString& szPrefix = iter.key();
		const QString& szNamespaceURI = iter.value();
		if(szNamespaceURI == szTargetNamespaceURI)
		{
			m_szCurrentTargetNamespacePrefix = szPrefix;
			m_szCurrentTargetNamespaceUri = szNamespaceURI;
			printf("[QWSDLParser] Target namespace prefix found in definitions: %s", qPrintable(m_szCurrentTargetNamespacePrefix));
			break;
		}
		iter++;
	}
}

void QWSDLParser::logParser(const QString& szMsg)
{
	QString szIdent;
	for(int i=0; i<m_iLogIndent; i++)
	{
		szIdent += " ";
	}
	printf("[QWSDLParser] %s%s\n", qPrintable(szIdent), qPrintable(szMsg));
}

void QWSDLParser::incrLogIndent()
{
	m_iLogIndent++;
}

void QWSDLParser::decrLogIndent()
{
	m_iLogIndent--;
}

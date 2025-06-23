#include <QCoreApplication>

#include "calculator/CalculatorService.h"

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	
	printf("Starting application\n");

	if(argc<3){
		printf("Usage: A B\n");
		return 0;
	}

	QString szArg1 = argv[1];
	QString szArg2 = argv[2];

	calculator::CalculatorService service;
	service.setBaseUrl(QUrl("http://www.dneonline.com/calculator.asmx"));
	service.setDebug(true);

	XS::Integer iA;
	iA.setValue(szArg1.toInt());
	XS::Integer iB;
	iB.setValue(szArg2.toInt());

	printf("Compute %d + %d: \n", iA.getValue(), iB.getValue());

	calculator::TNS::MSG::Add request;
	request.setIntA(iA);
	request.setIntB(iB);

	calculator::TNS::MSG::AddResponse response;
	service.Add(request, response);

	printf("Response: %d \n", response.getAddResult().getValue());


	printf("Exit application\n");


	return 0;
}

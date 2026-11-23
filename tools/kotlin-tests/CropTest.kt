import com.dlof.rinlang.*
fun main() {
    check(CropMath.centerCrop(2000,1000,500,500).toList() == listOf(500,0,1500,1000))
    check(CropMath.centerCrop(1000,2000,500,500).toList() == listOf(0,500,1000,1500))
    check(CropMath.centerCrop(100,100,50,50).toList() == listOf(0,0,100,100))
    check(CropMath.sampleSize(4000,3000,1600)==2 && CropMath.sampleSize(1600,900,1600)==1 && CropMath.sampleSize(8000,6000,1600)==4)
    println("OK")
}
